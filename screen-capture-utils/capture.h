// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_CAPTURE_H_
#define SCREEN_CAPTURE_UTILS_CAPTURE_H_

namespace screenshot {

class DisplayBuffer {
 public:
  struct Result {
    const uint32_t width;
    const uint32_t height;
    const uint32_t stride;
    void* buffer;
  };
  DisplayBuffer(const DisplayBuffer&) = delete;
  DisplayBuffer& operator=(const DisplayBuffer&) = delete;

  DisplayBuffer() = default;
  virtual ~DisplayBuffer() = default;
  virtual Result Capture() = 0;
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_CAPTURE_H_
