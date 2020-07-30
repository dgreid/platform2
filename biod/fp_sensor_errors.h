// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_FP_SENSOR_ERRORS_H_
#define BIOD_FP_SENSOR_ERRORS_H_

#include <brillo/enum_flags.h>
#include <chromeos/ec/ec_commands.h>

namespace biod {

static constexpr int kMaxDeadPixels = FP_ERROR_DEAD_PIXELS_UNKNOWN - 1;
static_assert(kMaxDeadPixels > 0,
              "Max number of dead pixels must be greater than zero");

enum class FpSensorErrors {
  kNone = 0,
  kNoIrq = 1u << 0u,
  kSpiCommunication = 1u << 1u,
  kBadHardwareID = 1u << 2u,
  kInitializationFailure = 1u << 3u,
  kDeadPixels = 1u << 4u,
};
DECLARE_FLAGS_ENUM(FpSensorErrors);

}  // namespace biod

#endif  // BIOD_FP_SENSOR_ERRORS_H_
