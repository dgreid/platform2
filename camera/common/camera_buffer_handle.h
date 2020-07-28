/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_BUFFER_HANDLE_H_
#define CAMERA_COMMON_CAMERA_BUFFER_HANDLE_H_

#include <system/window.h>

#include "cros-camera/common.h"

const uint32_t kCameraBufferMagic = 0xD1DAD1DA;
const uint64_t kInvalidBufferId = 0xFFFFFFFFFFFFFFFF;

const size_t kMaxPlanes = 4;

enum BufferState {
  kRegistered = 0,  // The buffer is registered by the framework.
  kReturned = 1,    // The buffer is returned to the framework.
};

typedef struct camera_buffer_handle {
  native_handle_t base;
  // The fds for each plane.
  int fds[kMaxPlanes];
  // Should be kCameraBufferMagic.  This is for basic consistency check.
  uint32_t magic;
  // Used to identify the buffer object on the other end of the IPC channel
  // (e.g. the Android container or Chrome browser process.)
  uint64_t buffer_id;
  // The type of the buffer.  Must be one of the values defined in enum
  // BufferType.
  int32_t type;
  // The DRM fourcc code of the buffer.
  uint32_t drm_format;
  // The HAL pixel format of the buffer.
  uint32_t hal_pixel_format;
  // The width of the buffer in pixels.
  uint32_t width;
  // The height of the buffer in pixels.
  uint32_t height;
  // The stride of each plane in bytes.
  uint32_t strides[kMaxPlanes];
  // The offset to the start of each plane in bytes.
  uint32_t offsets[kMaxPlanes];
  // The state of the buffer; must be one of |BufferState|.
  int state;
  // For passing the buffer handle in camera3_stream_buffer_t to the HAL since
  // it requires a buffer_handle_t*.
  buffer_handle_t self;

  camera_buffer_handle()
      : magic(kCameraBufferMagic),
        buffer_id(kInvalidBufferId),
        type(-1),
        drm_format(0),
        hal_pixel_format(0),
        width(0),
        height(0),
        strides{},
        offsets{},
        state(kRegistered),
        self(reinterpret_cast<buffer_handle_t>(this)) {
    for (size_t i = 0; i < kMaxPlanes; ++i) {
      fds[i] = -1;
    }
  }

  ~camera_buffer_handle() {
    for (size_t i = 0; i < kMaxPlanes; ++i) {
      if (fds[i] != -1) {
        close(fds[i]);
      }
    }
  }

  static const struct camera_buffer_handle* FromBufferHandle(
      buffer_handle_t handle) {
    auto h = reinterpret_cast<const struct camera_buffer_handle*>(handle);
    if (!h) {
      LOGF(ERROR) << "Invalid buffer handle";
      return nullptr;
    }
    if (h->magic != kCameraBufferMagic) {
      LOGF(ERROR) << "Invalid buffer handle: magic=" << h->magic;
      return nullptr;
    }
    return h;
  }
} camera_buffer_handle_t;

const size_t kCameraBufferHandleNumFds = kMaxPlanes;
const size_t kCameraBufferHandleNumInts =
    (sizeof(struct camera_buffer_handle) - sizeof(native_handle_t) -
     (sizeof(int32_t) * kMaxPlanes)) /
    sizeof(int);

#endif  // CAMERA_COMMON_CAMERA_BUFFER_HANDLE_H_
