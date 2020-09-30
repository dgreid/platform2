// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_

#include <cstdint>
#include <memory>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"

namespace diagnostics {

// Output messages for the battery capacity routine when in various states.
extern const char kBatteryCapacityRoutineParametersInvalidMessage[];
extern const char kBatteryCapacityFailedReadingChargeFullDesignMessage[];
extern const char kBatteryCapacityFailedParsingChargeFullDesignMessage[];
extern const char kBatteryCapacityRoutineSucceededMessage[];
extern const char kBatteryCapacityRoutineFailedMessage[];

// The battery capacity routine checks whether or not the battery's design
// capacity is within the given limits.
std::unique_ptr<DiagnosticRoutine> CreateBatteryCapacityRoutine(
    Context* const context, uint32_t low_mah, uint32_t high_mah);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_
