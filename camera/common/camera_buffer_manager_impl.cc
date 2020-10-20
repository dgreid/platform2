/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_buffer_manager_impl.h"

#include <vector>

#include <linux/videodev2.h>
#include <sys/mman.h>

#include <base/no_destructor.h>
#include <drm_fourcc.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>

#include "common/camera_buffer_handle.h"
#include "common/camera_buffer_manager_internal.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

std::unordered_map<uint32_t, std::vector<uint32_t>> kSupportedHalFormats{
    {HAL_PIXEL_FORMAT_BLOB, {DRM_FORMAT_R8}},
    {HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
     {DRM_FORMAT_NV12, DRM_FORMAT_XBGR8888, DRM_FORMAT_MTISP_SXYZW10}},
    {HAL_PIXEL_FORMAT_YCbCr_420_888, {DRM_FORMAT_NV12}},
};

uint32_t GetGbmUseFlags(uint32_t hal_format, uint32_t usage) {
  uint32_t flags = 0;
  if (hal_format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
      !(usage & GRALLOC_USAGE_HW_CAMERA_READ)) {
    // The default GBM flags for non-private-reprocessing camera buffers.
    flags = GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN;
  }

  if (usage & GRALLOC_USAGE_HW_CAMERA_READ) {
    flags |= GBM_BO_USE_CAMERA_READ;
  }
  if (usage & GRALLOC_USAGE_HW_CAMERA_WRITE) {
    flags |= GBM_BO_USE_CAMERA_WRITE;
  }
  if (usage & GRALLOC_USAGE_HW_TEXTURE) {
    flags |= GBM_BO_USE_TEXTURING;
  }
  if (usage & GRALLOC_USAGE_HW_RENDER) {
    flags |= GBM_BO_USE_RENDERING;
  }
  if (usage & GRALLOC_USAGE_HW_COMPOSER) {
    flags |= GBM_BO_USE_SCANOUT | GBM_BO_USE_TEXTURING;
  }
  if (usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
    flags |= GBM_BO_USE_HW_VIDEO_ENCODER;
  }
  return flags;
}

}  // namespace

// static
CameraBufferManager* CameraBufferManager::GetInstance() {
  static base::NoDestructor<CameraBufferManagerImpl> instance;
  if (!instance->gbm_device_) {
    LOGF(ERROR) << "Failed to create GBM device for CameraBufferManager";
    return nullptr;
  }
  return instance.get();
}

// static
uint32_t CameraBufferManager::GetWidth(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  return handle->width;
}

// static
uint32_t CameraBufferManager::GetHeight(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  return handle->height;
}

// static
uint32_t CameraBufferManager::GetNumPlanes(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  switch (handle->drm_format) {
    case DRM_FORMAT_ABGR1555:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_ABGR4444:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_ARGB1555:
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_ARGB4444:
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_AYUV:
    case DRM_FORMAT_BGR233:
    case DRM_FORMAT_BGR565:
    case DRM_FORMAT_BGR888:
    case DRM_FORMAT_BGRA1010102:
    case DRM_FORMAT_BGRA4444:
    case DRM_FORMAT_BGRA5551:
    case DRM_FORMAT_BGRA8888:
    case DRM_FORMAT_BGRX1010102:
    case DRM_FORMAT_BGRX4444:
    case DRM_FORMAT_BGRX5551:
    case DRM_FORMAT_BGRX8888:
    case DRM_FORMAT_C8:
    case DRM_FORMAT_GR88:
    case DRM_FORMAT_R8:
    case DRM_FORMAT_RG88:
    case DRM_FORMAT_RGB332:
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_RGB888:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBA4444:
    case DRM_FORMAT_RGBA5551:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_RGBX1010102:
    case DRM_FORMAT_RGBX4444:
    case DRM_FORMAT_RGBX5551:
    case DRM_FORMAT_RGBX8888:
    case DRM_FORMAT_UYVY:
    case DRM_FORMAT_VYUY:
    case DRM_FORMAT_XBGR1555:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_XBGR4444:
    case DRM_FORMAT_XBGR8888:
    case DRM_FORMAT_XRGB1555:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XRGB4444:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_YUYV:
    case DRM_FORMAT_YVYU:
    case DRM_FORMAT_MTISP_SXYZW10:
      return 1;
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
      return 2;
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
      return 3;
  }

  LOGF(ERROR) << "Unknown format: " << FormatToString(handle->drm_format);
  return 0;
}

// static
uint32_t CameraBufferManager::GetV4L2PixelFormat(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }

  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return 0;
  }

  bool is_mplane = false;
  if (num_planes > 1) {
    // Check if the buffer has multiple physical planes by checking the offsets
    // of each plane.  If any of the offsets is zero, then we assume the buffer
    // is of multi-planar format.
    for (size_t i = 1; i < num_planes; ++i) {
      if (!handle->offsets[i]) {
        is_mplane = true;
      }
    }
  }

  switch (handle->drm_format) {
    case DRM_FORMAT_ARGB8888:
      return V4L2_PIX_FMT_ABGR32;

    // There is no standard V4L2 pixel format corresponding to
    // DRM_FORMAT_xBGR8888.  We use our own V4L2 format extension
    // V4L2_PIX_FMT_RGBX32 here.
    case DRM_FORMAT_ABGR8888:
      return V4L2_PIX_FMT_RGBX32;
    case DRM_FORMAT_XBGR8888:
      return V4L2_PIX_FMT_RGBX32;

    // The format used by MediaTek ISP for private reprocessing. Note that the
    // V4L2 format used here is a default placeholder. The actual pixel format
    // varies depending on sensor settings.
    case DRM_FORMAT_MTISP_SXYZW10:
      return V4L2_PIX_FMT_MTISP_SBGGR10;

    // DRM_FORMAT_R8 is used as the underlying buffer format for
    // HAL_PIXEL_FORMAT_BLOB which corresponds to JPEG buffer.
    case DRM_FORMAT_R8:
      return V4L2_PIX_FMT_JPEG;

    // Semi-planar formats.
    case DRM_FORMAT_NV12:
      return is_mplane ? V4L2_PIX_FMT_NV12M : V4L2_PIX_FMT_NV12;
    case DRM_FORMAT_NV21:
      return is_mplane ? V4L2_PIX_FMT_NV21M : V4L2_PIX_FMT_NV21;

    // Multi-planar formats.
    case DRM_FORMAT_YUV420:
      return is_mplane ? V4L2_PIX_FMT_YUV420M : V4L2_PIX_FMT_YUV420;
    case DRM_FORMAT_YVU420:
      return is_mplane ? V4L2_PIX_FMT_YVU420M : V4L2_PIX_FMT_YVU420;
  }

  LOGF(ERROR) << "Could not convert format "
              << FormatToString(handle->drm_format) << " to V4L2 pixel format";
  return 0;
}

// static
size_t CameraBufferManager::GetPlaneStride(buffer_handle_t buffer,
                                           size_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }
  if (plane >= GetNumPlanes(buffer)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return 0;
  }
  return handle->strides[plane];
}

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

// static
size_t CameraBufferManager::GetPlaneSize(buffer_handle_t buffer, size_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return 0;
  }
  if (plane >= GetNumPlanes(buffer)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return 0;
  }
  uint32_t vertical_subsampling;
  switch (handle->drm_format) {
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV21:
    case DRM_FORMAT_YUV420:
    case DRM_FORMAT_YVU420:
      vertical_subsampling = (plane == 0) ? 1 : 2;
      break;
    default:
      vertical_subsampling = 1;
  }
  return (handle->strides[plane] *
          DIV_ROUND_UP(handle->height, vertical_subsampling));
}

// static
off_t CameraBufferManager::GetPlaneOffset(buffer_handle_t buffer,
                                          size_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -1;
  }
  if (plane >= GetNumPlanes(buffer)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return -1;
  }
  return handle->offsets[plane];
}

CameraBufferManagerImpl::CameraBufferManagerImpl()
    : gbm_device_(internal::CreateGbmDevice()) {}

CameraBufferManagerImpl::~CameraBufferManagerImpl() {
  if (gbm_device_) {
    close(gbm_device_get_fd(gbm_device_));
    gbm_device_destroy(gbm_device_);
  }
}

int CameraBufferManagerImpl::Allocate(size_t width,
                                      size_t height,
                                      uint32_t format,
                                      uint32_t usage,
                                      BufferType type,
                                      buffer_handle_t* out_buffer,
                                      uint32_t* out_stride) {
  if (type == GRALLOC) {
    return AllocateGrallocBuffer(width, height, format, usage, out_buffer,
                                 out_stride);
  } else if (type == SHM) {
    return AllocateShmBuffer(width, height, format, usage, out_buffer,
                             out_stride);
  } else {
    NOTREACHED() << "Invalid buffer type: " << type;
    return -EINVAL;
  }
}

int CameraBufferManagerImpl::Free(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  if (handle->type == GRALLOC) {
    Deregister(buffer);
    delete handle;
    return 0;
  } else {
    // TODO(jcliang): Implement deletion of SharedMemory.
    return -EINVAL;
  }
}

int CameraBufferManagerImpl::Register(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  base::AutoLock l(lock_);

  auto context_it = buffer_context_.find(buffer);
  if (context_it != buffer_context_.end()) {
    context_it->second->usage++;
    return 0;
  }

  std::unique_ptr<BufferContext> buffer_context(new struct BufferContext);

  if (handle->type == GRALLOC) {
    // Import the buffer if we haven't done so.
    struct gbm_import_fd_modifier_data import_data;
    memset(&import_data, 0, sizeof(import_data));
    import_data.width = handle->width;
    import_data.height = handle->height;
    import_data.format = handle->drm_format;
    uint32_t num_planes = GetNumPlanes(buffer);
    if (num_planes <= 0) {
      return -EINVAL;
    }

    import_data.num_fds = num_planes;
    for (size_t i = 0; i < num_planes; ++i) {
      import_data.fds[i] = handle->fds[i];
      import_data.strides[i] = handle->strides[i];
      import_data.offsets[i] = handle->offsets[i];
    }

    uint32_t usage = GBM_BO_USE_CAMERA_READ | GBM_BO_USE_CAMERA_WRITE |
                     GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN;
    buffer_context->bo = gbm_bo_import(gbm_device_, GBM_BO_IMPORT_FD_MODIFIER,
                                       &import_data, usage);
    if (!buffer_context->bo) {
      LOGF(ERROR) << "Failed to import buffer 0x" << std::hex
                  << handle->buffer_id;
      return -EIO;
    }
  } else if (handle->type == SHM) {
    // The shared memory buffer is a contiguous area of memory which is large
    // enough to hold all the physical planes.  We mmap the buffer on Register
    // and munmap on Deregister.
    off_t size = lseek(handle->fds[0], 0, SEEK_END);
    if (size == -1) {
      PLOGF(ERROR) << "Failed to get shm buffer size through lseek";
      return -errno;
    }
    buffer_context->shm_buffer_size = static_cast<uint32_t>(size);
    lseek(handle->fds[0], 0, SEEK_SET);
    buffer_context->mapped_addr =
        mmap(nullptr, buffer_context->shm_buffer_size, PROT_READ | PROT_WRITE,
             MAP_SHARED, handle->fds[0], 0);
    if (buffer_context->mapped_addr == MAP_FAILED) {
      PLOGF(ERROR) << "Failed to mmap shm buffer";
      return -errno;
    }
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }

  buffer_context->usage = 1;
  buffer_context_[buffer] = std::move(buffer_context);
  return 0;
}

int CameraBufferManagerImpl::Deregister(buffer_handle_t buffer) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  base::AutoLock l(lock_);

  auto context_it = buffer_context_.find(buffer);
  if (context_it == buffer_context_.end()) {
    LOGF(ERROR) << "Unknown buffer 0x" << std::hex << handle->buffer_id;
    return -EINVAL;
  }
  auto buffer_context = context_it->second.get();
  if (handle->type == GRALLOC) {
    if (!--buffer_context->usage) {
      // Unmap all the existing mapping of bo.
      for (auto it = buffer_info_.begin(); it != buffer_info_.end();) {
        if (it->second->bo == buffer_context->bo) {
          it = buffer_info_.erase(it);
        } else {
          ++it;
        }
      }
      buffer_context_.erase(context_it);
    }
    return 0;
  } else if (handle->type == SHM) {
    if (!--buffer_context->usage) {
      int ret =
          munmap(buffer_context->mapped_addr, buffer_context->shm_buffer_size);
      if (ret == -1) {
        PLOGF(ERROR) << "Failed to munmap shm buffer";
      }
      buffer_context_.erase(context_it);
    }
    return 0;
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }
}

int CameraBufferManagerImpl::Lock(buffer_handle_t buffer,
                                  uint32_t flags,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t width,
                                  uint32_t height,
                                  void** out_addr) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }
  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return -EINVAL;
  }
  if (num_planes > 1) {
    LOGF(ERROR) << "Lock called on multi-planar buffer 0x" << std::hex
                << handle->buffer_id;
    return -EINVAL;
  }

  *out_addr = Map(buffer, flags, 0);
  if (*out_addr == MAP_FAILED) {
    return -EINVAL;
  }
  return 0;
}

int CameraBufferManagerImpl::LockYCbCr(buffer_handle_t buffer,
                                       uint32_t flags,
                                       uint32_t x,
                                       uint32_t y,
                                       uint32_t width,
                                       uint32_t height,
                                       struct android_ycbcr* out_ycbcr) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }
  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return -EINVAL;
  }
  if (num_planes < 2) {
    LOGF(ERROR) << "LockYCbCr called on single-planar buffer 0x" << std::hex
                << handle->buffer_id;
    return -EINVAL;
  }

  DCHECK_LE(num_planes, 3u);
  std::vector<uint8_t*> addr(num_planes);
  for (size_t i = 0; i < num_planes; ++i) {
    void* a = Map(buffer, flags, i);
    if (a == MAP_FAILED) {
      return -EINVAL;
    }
    addr[i] = reinterpret_cast<uint8_t*>(a);
  }
  out_ycbcr->y = addr[0];
  out_ycbcr->ystride = handle->strides[0];
  out_ycbcr->cstride = handle->strides[1];

  if (num_planes == 2) {
    out_ycbcr->chroma_step = 2;
    switch (handle->drm_format) {
      case DRM_FORMAT_NV12:
        out_ycbcr->cb = addr[1];
        out_ycbcr->cr = addr[1] + 1;
        break;

      case DRM_FORMAT_NV21:
        out_ycbcr->cb = addr[1] + 1;
        out_ycbcr->cr = addr[1];
        break;

      default:
        LOGF(ERROR) << "Unsupported semi-planar format: "
                    << FormatToString(handle->drm_format);
        return -EINVAL;
    }
  } else {  // num_planes == 3
    out_ycbcr->chroma_step = 1;
    switch (handle->drm_format) {
      case DRM_FORMAT_YUV420:
        out_ycbcr->cb = addr[1];
        out_ycbcr->cr = addr[2];
        break;

      case DRM_FORMAT_YVU420:
        out_ycbcr->cb = addr[2];
        out_ycbcr->cr = addr[1];
        break;

      default:
        LOGF(ERROR) << "Unsupported planar format: "
                    << FormatToString(handle->drm_format);
        return -EINVAL;
    }
  }
  return 0;
}

int CameraBufferManagerImpl::Unlock(buffer_handle_t buffer) {
  for (size_t i = 0; i < GetNumPlanes(buffer); ++i) {
    int ret = Unmap(buffer, i);
    if (ret) {
      return ret;
    }
  }
  return 0;
}

uint32_t CameraBufferManagerImpl::ResolveDrmFormat(uint32_t hal_format,
                                                   uint32_t usage) {
  uint32_t unused_gbm_flags;
  return ResolveFormat(hal_format, usage, &unused_gbm_flags);
}

uint32_t CameraBufferManagerImpl::ResolveFormat(uint32_t hal_format,
                                                uint32_t usage,
                                                uint32_t* gbm_flags) {
  uint32_t gbm_usage = GetGbmUseFlags(hal_format, usage);
  uint32_t drm_format = 0;
  if (usage & GRALLOC_USAGE_FORCE_I420) {
    CHECK_EQ(hal_format, HAL_PIXEL_FORMAT_YCbCr_420_888);
    *gbm_flags = gbm_usage;
    return DRM_FORMAT_YUV420;
  }

  if (hal_format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
      (usage & GRALLOC_USAGE_HW_CAMERA_READ)) {
    // Check which private format the graphics backend support.
    if (gbm_device_is_format_supported(gbm_device_, DRM_FORMAT_MTISP_SXYZW10,
                                       gbm_usage)) {
      *gbm_flags = gbm_usage;
      return DRM_FORMAT_MTISP_SXYZW10;
    }
    // TODO(lnishan): Check other private formats when we have private formats
    // from other platforms.
  }

  if (kSupportedHalFormats.find(hal_format) == kSupportedHalFormats.end()) {
    LOGF(ERROR) << "Unsupported HAL pixel format";
    return 0;
  }

  for (uint32_t format : kSupportedHalFormats[hal_format]) {
    if (gbm_device_is_format_supported(gbm_device_, format, gbm_usage)) {
      drm_format = format;
      break;
    }
  }

  if (drm_format == 0 && usage & GRALLOC_USAGE_HW_COMPOSER) {
    gbm_usage &= ~GBM_BO_USE_SCANOUT;
    for (uint32_t format : kSupportedHalFormats[hal_format]) {
      if (gbm_device_is_format_supported(gbm_device_, format, gbm_usage)) {
        drm_format = format;
        break;
      }
    }
  }

  if (drm_format == 0) {
    LOGF(ERROR) << "Cannot resolve the actual format of HAL pixel format "
                << hal_format;
    return 0;
  }

  *gbm_flags = gbm_usage;
  return drm_format;
}

int CameraBufferManagerImpl::AllocateGrallocBuffer(size_t width,
                                                   size_t height,
                                                   uint32_t format,
                                                   uint32_t usage,
                                                   buffer_handle_t* out_buffer,
                                                   uint32_t* out_stride) {
  base::AutoLock l(lock_);

  uint32_t gbm_flags;
  uint32_t drm_format = ResolveFormat(format, usage, &gbm_flags);
  if (!drm_format) {
    return -EINVAL;
  }

  std::unique_ptr<BufferContext> buffer_context(new struct BufferContext);
  buffer_context->bo =
      gbm_bo_create(gbm_device_, width, height, drm_format, gbm_flags);
  if (!buffer_context->bo) {
    LOGF(ERROR) << "Failed to create GBM bo";
    return -ENOMEM;
  }

  std::unique_ptr<camera_buffer_handle_t> handle(new camera_buffer_handle_t());
  handle->base.version = sizeof(handle->base);
  handle->base.numInts = kCameraBufferHandleNumInts;
  handle->base.numFds = kCameraBufferHandleNumFds;
  handle->magic = kCameraBufferMagic;
  handle->buffer_id = reinterpret_cast<uint64_t>(buffer_context->bo);
  handle->type = GRALLOC;
  handle->drm_format = drm_format;
  handle->hal_pixel_format = format;
  handle->width = width;
  handle->height = height;
  size_t num_planes = gbm_bo_get_plane_count(buffer_context->bo);
  for (size_t i = 0; i < num_planes; ++i) {
    handle->fds[i] = gbm_bo_get_plane_fd(buffer_context->bo, i);
    handle->strides[i] = gbm_bo_get_stride_for_plane(buffer_context->bo, i);
    handle->offsets[i] = gbm_bo_get_offset(buffer_context->bo, i);
  }

  if (num_planes == 1) {
    *out_stride = handle->strides[0];
  } else {
    *out_stride = 0;
  }
  *out_buffer = reinterpret_cast<buffer_handle_t>(handle.release());
  buffer_context->usage = 1;
  buffer_context_[*out_buffer] = std::move(buffer_context);
  return 0;
}

int CameraBufferManagerImpl::AllocateShmBuffer(size_t width,
                                               size_t height,
                                               uint32_t format,
                                               uint32_t usage,
                                               buffer_handle_t* out_buffer,
                                               uint32_t* out_stride) {
  // TODO(jcliang): Implement allocation of SharedMemory.
  return -EINVAL;
}

void* CameraBufferManagerImpl::Map(buffer_handle_t buffer,
                                   uint32_t flags,
                                   uint32_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return MAP_FAILED;
  }

  uint32_t num_planes = GetNumPlanes(buffer);
  if (!num_planes) {
    return MAP_FAILED;
  }
  if (!(plane < kMaxPlanes && plane < num_planes)) {
    LOGF(ERROR) << "Invalid plane: " << plane;
    return MAP_FAILED;
  }

  VLOGF(2) << "buffer info:";
  VLOGF(2) << "\tfd: " << handle->fds[plane];
  VLOGF(2) << "\tbuffer_id: 0x" << std::hex << handle->buffer_id;
  VLOGF(2) << "\ttype: " << handle->type;
  VLOGF(2) << "\tformat: " << FormatToString(handle->drm_format);
  VLOGF(2) << "\twidth: " << handle->width;
  VLOGF(2) << "\theight: " << handle->height;
  VLOGF(2) << "\tstride: " << handle->strides[plane];
  VLOGF(2) << "\toffset: " << handle->offsets[plane];

  base::AutoLock l(lock_);

  if (handle->type == GRALLOC) {
    auto key = MappedGrallocBufferInfoCache::key_type(buffer, plane);
    auto info_cache = buffer_info_.find(key);
    if (info_cache == buffer_info_.end()) {
      // We haven't mapped |plane| of |buffer| yet.
      std::unique_ptr<MappedGrallocBufferInfo> info(
          new MappedGrallocBufferInfo);
      auto context_it = buffer_context_.find(buffer);
      if (context_it == buffer_context_.end()) {
        LOGF(ERROR) << "Buffer 0x" << std::hex << handle->buffer_id
                    << " is not registered";
        return MAP_FAILED;
      }
      info->bo = context_it->second->bo;
      // Since |flags| is reserved we don't expect user to pass any non-zero
      // value, we simply override |flags| here.
      flags = GBM_BO_TRANSFER_READ_WRITE;
      uint32_t stride;
      info->addr = gbm_bo_map2(info->bo, 0, 0, handle->width, handle->height,
                               flags, &stride, &info->map_data, plane);
      if (info->addr == MAP_FAILED) {
        PLOGF(ERROR) << "Failed to map buffer";
        return MAP_FAILED;
      }
      info->usage = 1;
      buffer_info_[key] = std::move(info);
    } else {
      // We have mapped |plane| on |buffer| before: we can simply call
      // gbm_bo_map() on the existing bo.
      DCHECK(buffer_context_.find(buffer) != buffer_context_.end());
      info_cache->second->usage++;
    }
    struct MappedGrallocBufferInfo* info = buffer_info_[key].get();
    VLOGF(2) << "Plane " << plane << " of gralloc buffer 0x" << std::hex
             << handle->buffer_id << " mapped to "
             << reinterpret_cast<uintptr_t>(info->addr);
    return info->addr;
  } else if (handle->type == SHM) {
    // We can't call mmap() here because each mmap call may return different
    // mapped virtual addresses and may lead to virtual memory address leak.
    // Instead we call mmap() only once in Register.
    auto context_it = buffer_context_.find(buffer);
    if (context_it == buffer_context_.end()) {
      LOGF(ERROR) << "Unknown buffer 0x" << std::hex << handle->buffer_id;
      return MAP_FAILED;
    }
    auto buffer_context = context_it->second.get();
    void* out_addr = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(buffer_context->mapped_addr) +
        handle->offsets[plane]);
    VLOGF(2) << "Plane " << plane << " of shm buffer 0x" << std::hex
             << handle->buffer_id << " mapped to "
             << reinterpret_cast<uintptr_t>(out_addr);
    return out_addr;
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return MAP_FAILED;
  }
}

int CameraBufferManagerImpl::Unmap(buffer_handle_t buffer, uint32_t plane) {
  auto handle = camera_buffer_handle_t::FromBufferHandle(buffer);
  if (!handle) {
    return -EINVAL;
  }

  if (handle->type == GRALLOC) {
    base::AutoLock l(lock_);
    auto key = MappedGrallocBufferInfoCache::key_type(buffer, plane);
    auto info_cache = buffer_info_.find(key);
    if (info_cache == buffer_info_.end()) {
      LOGF(ERROR) << "Plane " << plane << " of buffer 0x" << std::hex
                  << handle->buffer_id << " was not mapped";
      return -EINVAL;
    }
    auto& info = info_cache->second;
    if (!--info->usage) {
      buffer_info_.erase(info_cache);
    }
  } else if (handle->type == SHM) {
    // No-op for SHM buffers.
  } else {
    NOTREACHED() << "Invalid buffer type: " << handle->type;
    return -EINVAL;
  }
  VLOGF(2) << "buffer 0x" << std::hex << handle->buffer_id << " unmapped";
  return 0;
}

}  // namespace cros
