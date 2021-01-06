// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_EGL_CAPTURE_H_
#define SCREEN_CAPTURE_UTILS_EGL_CAPTURE_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <base/macros.h>
#include <gbm.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "screen-capture-utils/capture.h"
#include "screen-capture-utils/ptr_util.h"

namespace screenshot {

class Crtc;

class EglDisplayBuffer : public DisplayBuffer {
 public:
  EglDisplayBuffer(const Crtc* crtc,
                   uint32_t x,
                   uint32_t y,
                   uint32_t width,
                   uint32_t height);
  EglDisplayBuffer(const EglDisplayBuffer&) = delete;
  EglDisplayBuffer& operator=(const EglDisplayBuffer&) = delete;
  ~EglDisplayBuffer() override;
  // Captures a screenshot from the specified CRTC.
  DisplayBuffer::Result Capture() override;

 private:
  const Crtc& crtc_;
  const uint32_t x_;
  const uint32_t y_;
  const uint32_t width_;
  const uint32_t height_;
  const ScopedGbmDevicePtr device_;
  const EGLDisplay display_;

  GLuint input_texture_;
  GLuint output_texture_;
  unsigned int fbo_;
  EGLContext ctx_;
  PFNEGLCREATEIMAGEKHRPROC createImageKHR_;
  PFNEGLDESTROYIMAGEKHRPROC destroyImageKHR_;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_;
  bool import_modifiers_exist_;
  std::vector<char> buffer_;
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_EGL_CAPTURE_H_
