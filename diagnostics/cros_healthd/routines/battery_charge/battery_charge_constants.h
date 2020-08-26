// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_

namespace diagnostics {

// Status messages reported by the battery charge routine.
extern const char kBatteryChargeRoutineSucceededMessage[];
extern const char kBatteryChargeRoutineNotChargingMessage[];
extern const char kBatteryChargeRoutineFailedInsufficientChargeMessage[];
extern const char kBatteryChargeRoutineFailedReadingBatteryAttributesMessage[];
extern const char kBatteryChargeRoutineInvalidParametersMessage[];
extern const char kBatteryChargeRoutineCancelledMessage[];
extern const char kBatteryChargeRoutineRunningMessage[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CHARGE_BATTERY_CHARGE_CONSTANTS_H_
