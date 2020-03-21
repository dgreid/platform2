/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/supported_formats.h"

namespace cros {

uint32_t ResolveDrmFormat(int hal_pixel_format) {
  for (const auto& format_pair : kSupportedFormats) {
    if (format_pair.first == hal_pixel_format) {
      return format_pair.second;
    }
  }
  return 0;
}

int GetHalPixelFormat(uint32_t drm_format) {
  for (const auto& format_pair : kSupportedFormats) {
    if (format_pair.second == drm_format) {
      return format_pair.first;
    }
  }
  return 0;
}

}  // namespace cros
