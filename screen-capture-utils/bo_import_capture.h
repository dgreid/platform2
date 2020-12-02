// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_BO_IMPORT_CAPTURE_H_
#define SCREEN_CAPTURE_UTILS_BO_IMPORT_CAPTURE_H_

#include <stdint.h>

#include <memory>

#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <gbm.h>

#include "screen-capture-utils/capture.h"
#include "screen-capture-utils/ptr_util.h"

namespace screenshot {

class Crtc;

// Utility class to map/unmap GBM buffer with RAII.
class GbmBoDisplayBuffer : public DisplayBuffer {
 public:
  GbmBoDisplayBuffer(const Crtc* crtc,
                     uint32_t x,
                     uint32_t y,
                     uint32_t width,
                     uint32_t height);
  GbmBoDisplayBuffer(const GbmBoDisplayBuffer&) = delete;
  GbmBoDisplayBuffer& operator=(const GbmBoDisplayBuffer&) = delete;

  ~GbmBoDisplayBuffer() override;

  DisplayBuffer::Result Capture() override;

 private:
  const Crtc& crtc_;
  const ScopedGbmDevicePtr device_;
  const uint32_t x_;
  const uint32_t y_;
  const uint32_t width_;
  const uint32_t height_;

  ScopedGbmBoPtr bo_{nullptr};
  uint32_t stride_{0};
  void* map_data_{nullptr};
  void* buffer_{nullptr};
  base::ScopedFD buffer_fd_{0};
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_BO_IMPORT_CAPTURE_H_
