// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge_constants.h"

namespace diagnostics {

const char kBatteryChargeRoutineSucceededMessage[] =
    "Battery charge routine passed.";
const char kBatteryChargeRoutineNotChargingMessage[] =
    "Battery is not charging.";
const char kBatteryChargeRoutineFailedInsufficientChargeMessage[] =
    "Battery charge rate less than minimum required charge rate.";
const char kBatteryChargeRoutineFailedReadingBatteryAttributesMessage[] =
    "Failed to read battery attributes from sysfs.";
const char kBatteryChargeRoutineInvalidParametersMessage[] =
    "Invalid minimum required charge rate requested.";
const char kBatteryChargeRoutineCancelledMessage[] =
    "Battery charge routine cancelled.";
const char kBatteryChargeRoutineRunningMessage[] =
    "Battery charge routine running.";

}  // namespace diagnostics
