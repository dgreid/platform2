// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/battery_utils.h"

#include <cmath>

#include <base/strings/string_number_conversions.h>

#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

const char kBatteryDirectoryPath[] = "sys/class/power_supply/BAT0";
const char kBatteryChargeNowFileName[] = "charge_now";
const char kBatteryChargeFullFileName[] = "charge_full";
const char kBatteryChargeFullDesignFileName[] = "charge_full_design";
const char kBatteryStatusFileName[] = "status";
const char kBatteryStatusChargingValue[] = "Charging";
const char kBatteryStatusDischargingValue[] = "Discharging";

base::Optional<uint32_t> CalculateBatteryChargePercent(
    const base::FilePath& root_dir) {
  base::FilePath battery_path = root_dir.Append(kBatteryDirectoryPath);

  uint32_t charge_now;
  if (!ReadInteger(battery_path, kBatteryChargeNowFileName, base::StringToUint,
                   &charge_now)) {
    return base::nullopt;
  }

  uint32_t charge_full;
  if (!ReadInteger(battery_path, kBatteryChargeFullFileName, base::StringToUint,
                   &charge_full)) {
    return base::nullopt;
  }

  return static_cast<uint32_t>(
      std::round(100.0 * (static_cast<float>(charge_now) /
                          static_cast<float>(charge_full))));
}

}  // namespace diagnostics
