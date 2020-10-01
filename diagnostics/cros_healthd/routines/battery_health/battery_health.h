// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_

#include <cstdint>
#include <memory>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

// Status messages for the BatteryHealth routine when in various states.
extern const char kBatteryHealthInvalidParametersMessage[];
extern const char kBatteryHealthFailedCalculatingWearPercentageMessage[];
extern const char kBatteryHealthExcessiveWearMessage[];
extern const char kBatteryHealthFailedReadingCycleCountMessage[];
extern const char kBatteryHealthExcessiveCycleCountMessage[];
extern const char kBatteryHealthRoutinePassedMessage[];

// The battery health routine checks whether or not the battery's design
// capacity is within the given limits.
std::unique_ptr<DiagnosticRoutine> CreateBatteryHealthRoutine(
    Context* const context,
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_
