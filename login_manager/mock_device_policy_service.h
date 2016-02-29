// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_
#define LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_

#include "login_manager/device_policy_service.h"

#include <stdint.h>

#include <string>
#include <vector>

#include <crypto/scoped_nss_types.h>

#include "bindings/chrome_device_policy.pb.h"

namespace login_manager {
// Forward declaration.
typedef struct PK11SlotInfoStr PK11SlotInfo;

class MockDevicePolicyService : public DevicePolicyService {
 public:
  MockDevicePolicyService();
  virtual ~MockDevicePolicyService();
  MOCK_METHOD4(Store, bool(const uint8_t*, uint32_t, Completion, int));
  MOCK_METHOD1(Retrieve, bool(std::vector<uint8_t>*));
  MOCK_METHOD0(PersistKey, void(void));
  MOCK_METHOD1(PersistPolicy, void(Completion));
  MOCK_METHOD0(PersistPolicySync, bool(void));
  MOCK_METHOD4(
      CheckAndHandleOwnerLogin,
      bool(const std::string&, PK11SlotInfo*, bool*, PolicyService::Error*));
  MOCK_METHOD3(ValidateAndStoreOwnerKey,
               bool(const std::string&, const std::string&, PK11SlotInfo*));
  MOCK_METHOD0(KeyMissing, bool(void));
  MOCK_METHOD0(Mitigating, bool(void));
  MOCK_METHOD0(Initialize, bool(void));
  MOCK_METHOD2(ReportPolicyFileMetrics, void(bool, bool));

  // Work around lack of support for reference return values in GMock.
  const enterprise_management::ChromeDeviceSettingsProto& GetSettings() {
    proto_ = GetSettingsProxy();
    return proto_;
  }
  MOCK_METHOD0(GetSettingsProxy,
               const enterprise_management::ChromeDeviceSettingsProto(void));

 private:
  enterprise_management::ChromeDeviceSettingsProto proto_;
};
}  // namespace login_manager

#endif  // LOGIN_MANAGER_MOCK_DEVICE_POLICY_SERVICE_H_
