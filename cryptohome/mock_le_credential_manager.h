// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_LE_CREDENTIAL_MANAGER_H_
#define CRYPTOHOME_MOCK_LE_CREDENTIAL_MANAGER_H_

#include "cryptohome/le_credential_manager.h"

#include <string>

#include <base/files/file_path.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockLECredentialManager : public LECredentialManager {
 public:
  MOCK_METHOD(LECredError,
              InsertCredential,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               const DelaySchedule&,
               const ValidPcrCriteria&,
               uint64_t*),
              (override));

  MOCK_METHOD(LECredError,
              CheckCredential,
              (const uint64_t&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*,
               brillo::SecureBlob*),
              (override));

  MOCK_METHOD(LECredError,
              ResetCredential,
              (const uint64_t& label, const brillo::SecureBlob& reset_secret),
              (override));

  MOCK_METHOD(LECredError, RemoveCredential, (const uint64_t&), (override));

  MOCK_METHOD(bool, NeedsPcrBinding, (const uint64_t&), (override));

  MOCK_METHOD(int, GetWrongAuthAttempts, (const uint64_t& label), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_LE_CREDENTIAL_MANAGER_H_
