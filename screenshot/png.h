// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREENSHOT_PNG_H_
#define SCREENSHOT_PNG_H_

#include <stdint.h>

namespace screenshot {

// Saves a BGRX image on memory as a RGB PNG file.
void SaveAsPng(const char* path,
               void* data,
               uint32_t width,
               uint32_t height,
               uint32_t stride);

}  // namespace screenshot

#endif  // SCREENSHOT_PNG_H_
