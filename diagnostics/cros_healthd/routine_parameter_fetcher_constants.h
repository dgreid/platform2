// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_

namespace diagnostics {

// Path to each routine's properties in cros_config.
extern const char kBatteryCapacityPropertiesPath[];
extern const char kBatteryHealthPropertiesPath[];
extern const char kPrimeSearchPropertiesPath[];

// Battery capacity properties read from cros_config.
extern const char kLowMahProperty[];
extern const char kHighMahProperty[];

// Battery health properties read from cros_config.
extern const char kMaximumCycleCountProperty[];
extern const char kPercentBatteryWearAllowedProperty[];

// Prime search property read from cros_config.
extern const char kMaxNumProperty[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINE_PARAMETER_FETCHER_CONSTANTS_H_
