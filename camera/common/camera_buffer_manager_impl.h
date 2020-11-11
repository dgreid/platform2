/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_
#define CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_

#include "cros-camera/camera_buffer_manager.h"

#include <memory>
#include <unordered_map>
#include <utility>

#include <gbm.h>

#include <base/synchronization/lock.h>

// A V4L2 extension format which represents 32bit RGBX-8-8-8-8 format. This
// corresponds to DRM_FORMAT_XBGR8888 which is used as the underlying format for
// the HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINEND format on all CrOS boards.
#define V4L2_PIX_FMT_RGBX32 v4l2_fourcc('X', 'B', '2', '4')

// A 10-bit bayer format for private reprocessing on MediaTek ISP P1. It's a
// private RAW format that other DRM drivers will never support and thus making
// it not upstreamable (i.e., defined in official DRM headers). The define
// should be kept in sync with cs/chromeos_public/src/platform/minigbm/drv.h
#define DRM_FORMAT_MTISP_SXYZW10 fourcc_code('M', 'B', '1', '0')

struct native_handle;
typedef const native_handle* buffer_handle_t;
struct android_ycbcr;

namespace cros {

namespace tests {

class CameraBufferManagerImplTest;

}  // namespace tests

struct BufferContext {
  // ** The following fields are used for gralloc buffers only. **
  // The GBM bo of the gralloc buffer.
  struct gbm_bo* bo;
  // ** End of gralloc buffer fields. **

  // ** The following fields are used for shm buffers only. **
  // The mapped address of the shared memory buffer.
  void* mapped_addr;

  // The size of the shared memory buffer.
  size_t shm_buffer_size;
  // ** End of shm buffer fields. **

  uint32_t usage;

  BufferContext()
      : bo(nullptr), mapped_addr(nullptr), shm_buffer_size(0), usage(0) {}

  ~BufferContext() {
    if (bo) {
      gbm_bo_destroy(bo);
    }
  }
};

typedef std::unordered_map<buffer_handle_t,
                           std::unique_ptr<struct BufferContext>>
    BufferContextCache;

struct MappedGrallocBufferInfo {
  // The gbm_bo associated with the imported buffer (for gralloc buffer only).
  struct gbm_bo* bo;
  // The per-bo data returned by gbm_bo_map() (for gralloc buffer only).
  void* map_data;
  // The mapped virtual address.
  void* addr;
  // For refcounting.
  uint32_t usage;

  MappedGrallocBufferInfo() : bo(nullptr), map_data(nullptr), usage(0) {}

  ~MappedGrallocBufferInfo() {
    if (bo && map_data) {
      gbm_bo_unmap(bo, map_data);
    }
  }
};

typedef std::pair<buffer_handle_t, uint32_t> MappedBufferInfoKeyType;

struct MappedBufferInfoKeyHash {
  size_t operator()(const MappedBufferInfoKeyType& key) const {
    // The key is (buffer_handle_t pointer, plane number).  Plane number is less
    // than 4, so shifting the pointer value left by 8 and filling the lowest
    // byte with the plane number gives us a unique value to represent a key.
    return (reinterpret_cast<size_t>(key.first) << 8 | key.second);
  }
};

typedef std::unordered_map<MappedBufferInfoKeyType,
                           std::unique_ptr<MappedGrallocBufferInfo>,
                           struct MappedBufferInfoKeyHash>
    MappedGrallocBufferInfoCache;

class CameraBufferManagerImpl final : public CameraBufferManager {
 public:
  CameraBufferManagerImpl();
  CameraBufferManagerImpl(const CameraBufferManagerImpl&) = delete;
  CameraBufferManagerImpl& operator=(const CameraBufferManagerImpl&) = delete;

  // CameraBufferManager implementation.
  ~CameraBufferManagerImpl();
  int Allocate(size_t width,
               size_t height,
               uint32_t format,
               uint32_t usage,
               BufferType type,
               buffer_handle_t* out_buffer,
               uint32_t* out_stride);
  int Free(buffer_handle_t buffer);
  int Register(buffer_handle_t buffer);
  int Deregister(buffer_handle_t buffer);
  int Lock(buffer_handle_t buffer,
           uint32_t flags,
           uint32_t x,
           uint32_t y,
           uint32_t width,
           uint32_t height,
           void** out_addr);
  int LockYCbCr(buffer_handle_t buffer,
                uint32_t flags,
                uint32_t x,
                uint32_t y,
                uint32_t width,
                uint32_t height,
                struct android_ycbcr* out_ycbcr);
  int Unlock(buffer_handle_t buffer);
  uint32_t ResolveDrmFormat(uint32_t hal_format, uint32_t usage);

 private:
  friend class CameraBufferManager;

  // Allow unit tests to call constructor directly.
  friend class tests::CameraBufferManagerImplTest;

  // Resolves the HAL pixel format |hal_format| to the actual DRM format, based
  // on the gralloc usage flags set in |usage|. The |gbm_flags| will be set if
  // the format is resolved successfully.
  uint32_t ResolveFormat(uint32_t hal_format,
                         uint32_t usage,
                         uint32_t* gbm_flags);

  int AllocateGrallocBuffer(size_t width,
                            size_t height,
                            uint32_t format,
                            uint32_t usage,
                            buffer_handle_t* out_buffer,
                            uint32_t* out_stride);

  int AllocateShmBuffer(size_t width,
                        size_t height,
                        uint32_t format,
                        uint32_t usage,
                        buffer_handle_t* out_buffer,
                        uint32_t* out_stride);

  // Maps |buffer| and returns the mapped address.
  //
  // Args:
  //    |buffer|: The buffer handle to map.
  //    |flags|:  Currently omitted and is reserved for future use.
  //    |plane|: The plane to map.
  //
  // Returns:
  //    The mapped address on success; MAP_FAILED on failure.
  void* Map(buffer_handle_t buffer, uint32_t flags, uint32_t plane);

  // Unmaps |buffer|.
  //
  // Args:
  //    |buffer|: The buffer handle to unmap.
  //    |plane|: The plane to unmap.
  //
  // Returns:
  //    0 on success; -EINVAL if |buffer| is invalid.
  int Unmap(buffer_handle_t buffer, uint32_t plane);

  // Lock to guard access member variables.
  base::Lock lock_;

  // ** Start of lock_ scope **

  // The handle to the opened GBM device.
  struct gbm_device* gbm_device_;

  // A cache which stores all the context of the registered buffers.
  // For gralloc buffers the context stores the imported GBM buffer objects.
  // For shm buffers the context stores the mapped address and the buffer size.
  // |buffer_context_| needs to be placed before |buffer_info_| to make sure the
  // GBM buffer objects are valid when we unmap them in |buffer_info_|'s
  // destructor.
  BufferContextCache buffer_context_;

  // The private info about all the mapped (buffer, plane) pairs.
  // |buffer_info_| has to be placed after |gbm_device_| so that the GBM device
  // is still valid when we delete the MappedGrallocBufferInfoCache.
  // This is only used by gralloc buffers.
  MappedGrallocBufferInfoCache buffer_info_;

  // ** End of lock_ scope **
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_BUFFER_MANAGER_IMPL_H_
