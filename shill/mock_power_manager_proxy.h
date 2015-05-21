// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_POWER_MANAGER_PROXY_H_
#define SHILL_MOCK_POWER_MANAGER_PROXY_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/power_manager_proxy_interface.h"

namespace shill {

class MockPowerManagerProxy : public PowerManagerProxyInterface {
 public:
  MockPowerManagerProxy();
  ~MockPowerManagerProxy() override;

  MOCK_METHOD3(RegisterSuspendDelay,
               bool(base::TimeDelta timeout,
                    const std::string &description,
                    int *delay_id_out));
  MOCK_METHOD1(UnregisterSuspendDelay, bool(int delay_id));
  MOCK_METHOD2(ReportSuspendReadiness, bool(int delay_id, int suspend_id));
  MOCK_METHOD3(RegisterDarkSuspendDelay,
               bool(base::TimeDelta timeout,
                    const std::string &description,
                    int *delay_id_out));
  MOCK_METHOD1(UnregisterDarkSuspendDelay, bool(int delay_id));
  MOCK_METHOD2(ReportDarkSuspendReadiness, bool(int delay_id, int suspend_id));
  MOCK_METHOD1(RecordDarkResumeWakeReason,
               bool(const std::string &wake_reason));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockPowerManagerProxy);
};

}  // namespace shill

#endif  // SHILL_MOCK_POWER_MANAGER_PROXY_H_
