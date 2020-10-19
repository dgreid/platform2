// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routine_parameter_fetcher.h"

#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/cros_healthd/routine_parameter_fetcher_constants.h"

namespace diagnostics {

RoutineParameterFetcher::RoutineParameterFetcher(
    brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config) {
  DCHECK(cros_config_);
}

RoutineParameterFetcher::~RoutineParameterFetcher() = default;

void RoutineParameterFetcher::GetBatteryCapacityParameters(
    base::Optional<uint32_t>* low_mah_out,
    base::Optional<uint32_t>* high_mah_out) const {
  FetchUint32Parameter(kBatteryCapacityPropertiesPath, kLowMahProperty,
                       low_mah_out);
  FetchUint32Parameter(kBatteryCapacityPropertiesPath, kHighMahProperty,
                       high_mah_out);
}

void RoutineParameterFetcher::GetBatteryHealthParameters(
    base::Optional<uint32_t>* maximum_cycle_count_out,
    base::Optional<uint8_t>* percent_battery_wear_allowed_out) const {
  FetchUint32Parameter(kBatteryHealthPropertiesPath, kMaximumCycleCountProperty,
                       maximum_cycle_count_out);
  FetchUint8Parameter(kBatteryHealthPropertiesPath,
                      kPercentBatteryWearAllowedProperty,
                      percent_battery_wear_allowed_out);
}

void RoutineParameterFetcher::FetchUint32Parameter(
    const std::string& path,
    const std::string& parameter_name,
    base::Optional<uint32_t>* parameter_out) const {
  DCHECK(parameter_out);

  // Assume the property cannot be fetched.
  *parameter_out = base::nullopt;

  std::string parameter_str;
  if (cros_config_->GetString(path, parameter_name, &parameter_str)) {
    uint32_t parameter;
    if (base::StringToUint(parameter_str, &parameter))
      *parameter_out = parameter;
  } else {
    LOG(ERROR) << base::StringPrintf(
        "Failed to convert cros_config value: %s to uint32_t.",
        parameter_str.c_str());
  }
}

void RoutineParameterFetcher::FetchUint8Parameter(
    const std::string& path,
    const std::string& parameter_name,
    base::Optional<uint8_t>* parameter_out) const {
  DCHECK(parameter_out);

  // Assume the property cannot be fetched.
  *parameter_out = base::nullopt;

  std::string parameter_str;
  if (cros_config_->GetString(path, parameter_name, &parameter_str)) {
    uint32_t parameter;
    if (base::StringToUint(parameter_str, &parameter))
      *parameter_out = static_cast<uint8_t>(parameter);
  } else {
    LOG(ERROR) << base::StringPrintf(
        "Failed to convert cros_config value: %s to uint32_t.",
        parameter_str.c_str());
  }
}

}  // namespace diagnostics
