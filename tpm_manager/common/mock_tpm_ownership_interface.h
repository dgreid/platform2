// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_COMMON_MOCK_TPM_OWNERSHIP_INTERFACE_H_
#define TPM_MANAGER_COMMON_MOCK_TPM_OWNERSHIP_INTERFACE_H_

#include <gmock/gmock.h>

#include "tpm_manager/common/tpm_ownership_interface.h"

namespace tpm_manager {

class MockTpmOwnershipInterface : public TpmOwnershipInterface {
 public:
  MockTpmOwnershipInterface();
  ~MockTpmOwnershipInterface() override;

  MOCK_METHOD(void,
              GetTpmStatus,
              (const GetTpmStatusRequest&, const GetTpmStatusCallback&),
              (override));
  MOCK_METHOD(void,
              GetTpmNonsensitiveStatus,
              (const GetTpmNonsensitiveStatusRequest&,
               const GetTpmNonsensitiveStatusCallback&),
              (override));
  MOCK_METHOD(void,
              GetVersionInfo,
              (const GetVersionInfoRequest&, const GetVersionInfoCallback&),
              (override));
  MOCK_METHOD(void,
              GetDictionaryAttackInfo,
              (const GetDictionaryAttackInfoRequest&,
               const GetDictionaryAttackInfoCallback&),
              (override));
  MOCK_METHOD(void,
              ResetDictionaryAttackLock,
              (const ResetDictionaryAttackLockRequest&,
               const ResetDictionaryAttackLockCallback&),
              (override));
  MOCK_METHOD(void,
              TakeOwnership,
              (const TakeOwnershipRequest&, const TakeOwnershipCallback&),
              (override));
  MOCK_METHOD(void,
              RemoveOwnerDependency,
              (const RemoveOwnerDependencyRequest&,
               const RemoveOwnerDependencyCallback&),
              (override));
  MOCK_METHOD(void,
              ClearStoredOwnerPassword,
              (const ClearStoredOwnerPasswordRequest&,
               const ClearStoredOwnerPasswordCallback&),
              (override));
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_COMMON_MOCK_TPM_OWNERSHIP_INTERFACE_H_
