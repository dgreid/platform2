// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routine_parameter_fetcher_constants.h"

namespace diagnostics {

const char kBatteryCapacityPropertiesPath[] =
    "/cros-healthd/routines/battery-capacity";
const char kBatteryHealthPropertiesPath[] =
    "/cros-healthd/routines/battery-health";
const char kPrimeSearchPropertiesPath[] = "/cros-healthd/routines/prime-search";

const char kLowMahProperty[] = "low-mah";
const char kHighMahProperty[] = "high-mah";

const char kMaximumCycleCountProperty[] = "maximum-cycle-count";
const char kPercentBatteryWearAllowedProperty[] =
    "percent-battery-wear-allowed";

const char kMaxNumProperty[] = "max-num";

}  // namespace diagnostics
