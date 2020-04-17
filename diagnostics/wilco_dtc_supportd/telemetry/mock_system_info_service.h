// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_INFO_SERVICE_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_INFO_SERVICE_H_

#include <string>

#include <gmock/gmock.h>

#include "diagnostics/wilco_dtc_supportd/telemetry/system_info_service.h"

namespace diagnostics {

class MockSystemInfoService : public SystemInfoService {
 public:
  MockSystemInfoService();
  ~MockSystemInfoService() override;

  MockSystemInfoService(const MockSystemInfoService&) = delete;
  MockSystemInfoService& operator=(const MockSystemInfoService&) = delete;

  MOCK_METHOD(bool, GetOsVersion, (std::string*), (override));

  MOCK_METHOD(bool, GetOsMilestone, (int*), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_TELEMETRY_MOCK_SYSTEM_INFO_SERVICE_H_
