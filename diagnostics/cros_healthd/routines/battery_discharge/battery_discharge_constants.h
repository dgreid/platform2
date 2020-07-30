// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_DISCHARGE_BATTERY_DISCHARGE_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_DISCHARGE_BATTERY_DISCHARGE_CONSTANTS_H_

namespace diagnostics {

// Status messages reported by the battery discharge routine.
extern const char kBatteryDischargeRoutineSucceededMessage[];
extern const char kBatteryDischargeRoutineNotDischargingMessage[];
extern const char kBatteryDischargeRoutineFailedExcessiveDischargeMessage[];
extern const char
    kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage[];
extern const char kBatteryDischargeRoutineInvalidParametersMessage[];
extern const char kBatteryDischargeRoutineCancelledMessage[];
extern const char kBatteryDischargeRoutineRunningMessage[];

// File and directory names used by the battery discharge routine.
extern const char kBatteryDirectoryPath[];
extern const char kBatteryChargeNowFileName[];
extern const char kBatteryChargeFullFileName[];
extern const char kBatteryStatusFileName[];

// Value of the status file that indicates the battery is discharging.
extern const char kBatteryStatusDischargingValue[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_DISCHARGE_BATTERY_DISCHARGE_CONSTANTS_H_
