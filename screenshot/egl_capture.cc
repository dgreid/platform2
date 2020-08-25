// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot/egl_capture.h"

#include <sys/mman.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <string>
#include <utility>
#include <vector>

#include "base/strings/string_split.h"
#include "screenshot/crtc.h"

namespace screenshot {
namespace {

GLuint LoadShader(const GLenum type, const char* const src) {
  GLuint shader = 0;
  shader = glCreateShader(type);
  CHECK(shader) << "Failed to to create shader";

  glShaderSource(shader, 1, &src, 0);
  glCompileShader(shader);

  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (compiled != GL_TRUE) {
    GLint log_length;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> shader_log(log_length);
    glGetShaderInfoLog(shader, log_length, nullptr, shader_log.data());
    CHECK(false) << "Shader failed to compile: " << shader_log.data();
  }

  return shader;
}

void LoadProgram(const GLchar* vert, const GLchar* frag) {
  GLint program = glCreateProgram();
  GLuint vertex_shader = LoadShader(GL_VERTEX_SHADER, vert);
  GLuint frag_shader = LoadShader(GL_FRAGMENT_SHADER, frag);
  glAttachShader(program, vertex_shader);
  glAttachShader(program, frag_shader);
  glLinkProgram(program);

  GLint linked = -1;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> program_log(log_length);
    glGetProgramInfoLog(program, log_length, nullptr, program_log.data());
    CHECK(false) << "GL program failed to link: " << program_log.data();
  }
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "tex"), 0);

  glDeleteProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(frag_shader);
}

bool DoesExtensionExist(const char* extension_string, const char* name) {
  std::vector<std::string> extensions = base::SplitString(
      extension_string, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  return std::find(extensions.begin(), extensions.end(), std::string(name)) !=
         extensions.end();
}

EGLImageKHR CreateImage(PFNEGLCREATEIMAGEKHRPROC CreateImageKHR,
                        bool import_modifiers_exist,
                        int drm_fd,
                        EGLDisplay display,
                        const drmModeFB2Ptr fb) {
  int num_planes = 0;
  // CreateImageKHR takes its own references to the dma-bufs, so closing the fds
  // at the end of the function is necessary and won't break the returned image.
  base::ScopedFD fds[GBM_MAX_PLANES] = {};
  for (size_t plane = 0; plane < GBM_MAX_PLANES; plane++) {
    // getfb2() doesn't return the number of planes so get handles
    // and count planes until we find a handle that isn't set
    if (fb->handles[plane] == 0)
      break;

    int fd;
    int ret = drmPrimeHandleToFD(drm_fd, fb->handles[plane], 0, &fd);
    CHECK_EQ(ret, 0) << "drmPrimeHandleToFD failed";
    fds[plane].reset(fd);
    num_planes++;
  }

  CHECK_GT(num_planes, 0);

  EGLint attr_list[46] = {
      EGL_WIDTH,
      static_cast<EGLint>(fb->width),
      EGL_HEIGHT,
      static_cast<EGLint>(fb->height),
      EGL_LINUX_DRM_FOURCC_EXT,
      static_cast<EGLint>(fb->pixel_format),
  };

  size_t attrs_index = 6;

  for (size_t plane = 0; plane < num_planes; plane++) {
    attr_list[attrs_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + plane * 3;
    attr_list[attrs_index++] = fds[plane].get();
    attr_list[attrs_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + plane * 3;
    attr_list[attrs_index++] = fb->offsets[plane];
    attr_list[attrs_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + plane * 3;
    attr_list[attrs_index++] = fb->pitches[plane];
    if (import_modifiers_exist) {
      attr_list[attrs_index++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + plane * 2;
      attr_list[attrs_index++] = fb->modifier & 0xfffffffful;
      attr_list[attrs_index++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT + plane * 2;
      attr_list[attrs_index++] = fb->modifier >> 32;
    }
  }

  attr_list[attrs_index] = EGL_NONE;

  EGLImageKHR image = CreateImageKHR(display, EGL_NO_CONTEXT,
                                     EGL_LINUX_DMA_BUF_EXT, 0, attr_list);
  CHECK(image != EGL_NO_IMAGE_KHR) << "Failed to create image";

  return image;
}

}  // namespace

EglPixelBuf::EglPixelBuf(ScopedGbmDevicePtr device,
                         std::vector<char> buffer,
                         uint32_t x,
                         uint32_t y,
                         uint32_t width,
                         uint32_t height,
                         uint32_t stride)
    : device_(std::move(device)),
      width_(width),
      height_(height),
      stride_(stride),
      buffer_(buffer) {}

std::unique_ptr<EglPixelBuf> EglCapture(
    const Crtc& crtc, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  ScopedGbmDevicePtr device(gbm_create_device(crtc.file().GetPlatformFile()));
  CHECK(device) << "gbm_create_device failed";

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  CHECK(display != EGL_NO_DISPLAY) << "Could not get EGLDisplay";

  EGLBoolean egl_ret = eglInitialize(display, NULL, NULL);
  CHECK(egl_ret) << "Could not initialize EGLDisplay";

  const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_DONT_CARE,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                   EGL_NONE};

  const EGLint GLES2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  EGLint num_configs;
  EGLConfig config;

  egl_ret = eglChooseConfig(display, config_attribs, &config, 1, &num_configs);
  CHECK(egl_ret) << "Could not choose EGLConfig";
  CHECK(num_configs != 0) << "Could not choose an EGL configuration";

  EGLContext ctx = eglCreateContext(display, config, EGL_NO_CONTEXT, GLES2);
  CHECK(ctx != EGL_NO_CONTEXT) << "Could not create EGLContext";

  egl_ret = eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
  CHECK(egl_ret) << "Could not bind context";

  std::string egl_extensions = eglQueryString(display, EGL_EXTENSIONS);
  CHECK(egl_extensions.find("EGL_KHR_image_base") != std::string::npos)
      << "Missing EGL extension: EGL_KHR_image_base";
  CHECK(egl_extensions.find("EGL_EXT_image_dma_buf_import") !=
        std::string::npos)
      << "Missing EGL extension: EGL_EXT_image_dma_buf_import";

  std::string gl_extensions = (const char*)glGetString(GL_EXTENSIONS);
  CHECK(gl_extensions.find("GL_OES_EGL_image") != std::string::npos)
      << "Missing GL extension: GL_OES_EGL_image";
  CHECK(gl_extensions.find("GL_OES_EGL_image_external") != std::string::npos)
      << "Missing GL extension: GL_OES_EGL_image_external";

  PFNEGLCREATEIMAGEKHRPROC CreateImageKHR =
      (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  CHECK(CreateImageKHR) << "CreateImageKHR not supported";
  PFNEGLDESTROYIMAGEKHRPROC DestroyImageKHR =
      (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  CHECK(DestroyImageKHR) << "DestroyImageKHR not supported";

  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  CHECK(extensions) << "eglQueryString() failed to get egl extensions";
  const bool import_modifiers_exist =
      DoesExtensionExist(extensions, "EGL_EXT_image_dma_buf_import_modifiers");

  GLuint output_texture;
  glGenTextures(1, &output_texture);
  glBindTexture(GL_TEXTURE_2D, output_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);

  GLuint input_texture;
  glGenTextures(1, &input_texture);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, input_texture);

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress(
          "glEGLImageTargetTexture2DOES");
  CHECK(glEGLImageTargetTexture2DOES)
      << "glEGLImageTargetTexture2DOES not supported";

  unsigned int fbo;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);

  const GLchar* vert =
      "#version 300 es\n"
      "out vec2 tex_pos;\n"
      "void main() {\n"
      " vec2 pos[4];\n"
      " pos[0] = vec2(-1.0, -1.0);\n"
      " pos[1] = vec2(1.0, -1.0);\n"
      " pos[2] = vec2(-1.0, 1.0);\n"
      " pos[3] = vec2(1.0, 1.0);\n"
      " gl_Position.xy = pos[gl_VertexID];\n"
      " gl_Position.zw = vec2(0.0, 1.0);\n"
      " vec2 uvs[4];\n"
      " uvs[0] = vec2(0.0, 0.0);\n"
      " uvs[1] = vec2(1.0, 0.0);\n"
      " uvs[2] = vec2(0.0, 1.0);\n"
      " uvs[3] = vec2(1.0, 1.0);\n"
      " tex_pos = uvs[gl_VertexID];\n"
      "}\n";

  const GLchar* frag =
      "#version 300 es\n"
      "#extension GL_OES_EGL_image_external_essl3 : require\n"
      "precision highp float;\n"
      "uniform samplerExternalOES tex;\n"
      "in vec2 tex_pos;\n"
      "out vec4 fragColor;\n"
      "void main() {\n"
      "  fragColor = texture(tex, tex_pos);\n"
      "}\n";

  LoadProgram(vert, frag);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         output_texture, 0);

  GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  CHECK(fb_status == GL_FRAMEBUFFER_COMPLETE) << "fb did not complete";

  GLuint indices[4] = {0, 1, 2, 3};

  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  if (crtc.planes().empty()) {
    EGLImageKHR image =
        CreateImage(CreateImageKHR, import_modifiers_exist,
                    crtc.file().GetPlatformFile(), display, crtc.fb2());
    CHECK(image != EGL_NO_IMAGE_KHR) << "Failed to create image";

    glViewport(0, 0, width, height);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, indices);

    DestroyImageKHR(display, image);
  } else {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto& plane : crtc.planes()) {
      EGLImageKHR image = CreateImage(CreateImageKHR, import_modifiers_exist,
                                      crtc.file().GetPlatformFile(), display,
                                      plane.first.get());
      CHECK(image != EGL_NO_IMAGE_KHR) << "Failed to create image";

      // TODO(dcastagna): Handle SRC_ and rotation.
      glViewport(plane.second.x, plane.second.y, plane.second.w,
                 plane.second.h);

      glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);

      glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, indices);

      DestroyImageKHR(display, image);
    }
  }

  std::vector<char> buffer(width * height * 4);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(x, y, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
               buffer.data());

  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

  glDeleteTextures(1, &input_texture);
  glDeleteTextures(1, &output_texture);
  glDeleteFramebuffers(1, &fbo);
  eglDestroyContext(display, ctx);
  eglTerminate(display);

  return std::make_unique<EglPixelBuf>(std::move(device), buffer, x, y, width,
                                       height, width * 4);
}

}  // namespace screenshot
