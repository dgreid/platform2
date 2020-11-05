// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_

#include <cstdint>

#include <base/files/file_path.h>
#include <base/optional.h>

namespace diagnostics {

// Path to the sysfs directory with battery information.
extern const char kBatteryDirectoryPath[];

// Files read from kBatteryDirectoryPath.
extern const char kBatteryChargeFullFileName[];
extern const char kBatteryChargeFullDesignFileName[];
extern const char kBatteryChargeNowFileName[];
extern const char kBatteryCurrentNowFileName[];
extern const char kBatteryCycleCountFileName[];
extern const char kBatteryEnergyFullFileName[];
extern const char kBatteryEnergyFullDesignFileName[];
extern const char kBatteryManufacturerFileName[];
extern const char kBatteryPresentFileName[];
extern const char kBatteryStatusFileName[];
extern const char kBatteryVoltageNowFileName[];

// Value of the status file that indicates the battery is charging.
extern const char kBatteryStatusChargingValue[];
// Value of the status file that indicates the battery is discharging.
extern const char kBatteryStatusDischargingValue[];

// Calculates the charge percent of the battery. Returns true and populates
// |charge_percent_out| iff the battery charge percent was able to be
// calculated.
base::Optional<double> CalculateBatteryChargePercent(
    const base::FilePath& root_dir);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
