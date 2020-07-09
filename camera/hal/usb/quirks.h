/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_QUIRKS_H_
#define CAMERA_HAL_USB_QUIRKS_H_

#include <cstdint>
#include <string>

#include "hal/usb/common_types.h"

namespace cros {

// The bitmask for each quirk.
enum : uint32_t {
  kQuirkMonocle = 1 << 0,
  kQuirkPreferMjpeg = 1 << 1,
  kQuirkRestartOnTimeout = 1 << 2,
  kQuirkReportLeastFpsRanges = 1 << 3,
  kQuirkDisableFrameRateSetting = 1 << 4,
  kQuirkV1Device = 1 << 5,
  kQuirkUserSpaceTimestamp = 1 << 6,
};

uint32_t GetQuirks(const std::string& vid, const std::string& pid);

}  // namespace cros

#endif  // CAMERA_HAL_USB_QUIRKS_H_
