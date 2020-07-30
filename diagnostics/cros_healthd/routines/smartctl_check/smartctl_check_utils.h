// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_UTILS_H_

#include <string>

namespace diagnostics {

// A scraper that is coupled to the format of smartctl -A.
bool ScrapeAvailableSparePercents(const std::string& output,
                                  int* available_spare_pct,
                                  int* available_spare_threshold_pct);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_SMARTCTL_CHECK_SMARTCTL_CHECK_UTILS_H_
