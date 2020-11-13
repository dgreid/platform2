// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_H_

#include <cstdint>
#include <string>

#include <base/optional.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>

namespace diagnostics {

// Responsible for fetching routine parameters from cros_config. Each individual
// parameter fetched for any of the routines will be either a valid value, if
// cros_config contained a value which could be read and parsed for that board,
// or base::nullopt if cros_config either didn't have that value, or the value
// couldn't be parsed from cros_config (e.g. a string was read, but a uin32_t
// was expected).
class RoutineParameterFetcher {
 public:
  explicit RoutineParameterFetcher(brillo::CrosConfigInterface* cros_config);
  RoutineParameterFetcher(const RoutineParameterFetcher&) = delete;
  RoutineParameterFetcher& operator=(const RoutineParameterFetcher&) = delete;
  ~RoutineParameterFetcher();

  // Fetches the parameters for the battery capacity routine.
  void GetBatteryCapacityParameters(
      base::Optional<uint32_t>* low_mah_out,
      base::Optional<uint32_t>* high_mah_out) const;

  // Fetches the parameters for the battery health routine.
  void GetBatteryHealthParameters(
      base::Optional<uint32_t>* maximum_cycle_count_out,
      base::Optional<uint8_t>* percent_battery_wear_allowed_out) const;

  // Fetches the parameter for the prime search routine.
  void GetPrimeSearchParameters(base::Optional<uint64_t>* max_num_out) const;

 private:
  // Fetches a uint64_t parameter from cros_config.
  void FetchUint64Parameter(const std::string& path,
                            const std::string& parameter_name,
                            base::Optional<uint64_t>* parameter_out) const;

  // Fetches a uint32_t parameter from cros_config.
  void FetchUint32Parameter(const std::string& path,
                            const std::string& parameter_name,
                            base::Optional<uint32_t>* parameter_out) const;

  // Fetches a uint8_t parameter from cros_config.
  void FetchUint8Parameter(const std::string& path,
                           const std::string& parameter_name,
                           base::Optional<uint8_t>* parameter_out) const;

  // Unowned. Should outlive this instance.
  brillo::CrosConfigInterface* cros_config_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_H_
