// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_SYSTEM_INFO_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_SYSTEM_INFO_SERVICE_H_

#include <string>

namespace diagnostics {

class SystemInfoService {
 public:
  virtual ~SystemInfoService() = default;

  // Gets the OS version. Returns true if successful.
  virtual bool GetOsVersion(std::string* version_out) = 0;

  // Gets the OS milestone. Returns true if successful.
  virtual bool GetOsMilestone(int* milestone_out) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_SYSTEM_INFO_SERVICE_H_
