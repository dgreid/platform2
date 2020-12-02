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

#include "screen-capture-utils/ptr_util.h"

namespace screenshot {

class Crtc;

// Utility class to fill pixel buffer with RAII.
class EglPixelBuf {
 public:
  EglPixelBuf(ScopedGbmDevicePtr device,
              std::vector<char> buffer,
              uint32_t x,
              uint32_t y,
              uint32_t width,
              uint32_t height,
              uint32_t stride);
  EglPixelBuf(const EglPixelBuf&) = delete;
  EglPixelBuf& operator=(const EglPixelBuf&) = delete;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t stride() const { return stride_; }
  std::vector<char>& buffer() { return buffer_; }

 private:
  const ScopedGbmDevicePtr device_;
  const uint32_t width_;
  const uint32_t height_;
  uint32_t stride_ = 0;
  std::vector<char> buffer_;
};

// Captures a screenshot from the specified CRTC.
std::unique_ptr<EglPixelBuf> EglCapture(
    const Crtc& crtc, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_EGL_CAPTURE_H_
