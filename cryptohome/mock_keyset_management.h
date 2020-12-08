// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_
#define CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_

#include "cryptohome/keyset_management.h"

#include <memory>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gmock/gmock.h>

#include "cryptohome/credentials.h"
#include "cryptohome/storage/mount.h"

namespace cryptohome {
class VaultKeyset;

class MockKeysetManagement : public KeysetManagement {
 public:
  MockKeysetManagement() = default;
  virtual ~MockKeysetManagement() = default;

  MOCK_METHOD(bool, AreCredentialsValid, (const Credentials&), (override));
  MOCK_METHOD(bool,
              Migrate,
              (const Credentials&, const brillo::SecureBlob&, int*),
              (override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              LoadUnwrappedKeyset,
              (const Credentials&, MountError*),
              (override));
  MOCK_METHOD(std::unique_ptr<VaultKeyset>,
              GetVaultKeyset,
              (const std::string&, const std::string&),
              (const, override));
  MOCK_METHOD(bool,
              GetVaultKeysets,
              (const std::string&, std::vector<int>*),
              (const, override));
  MOCK_METHOD(bool,
              GetVaultKeysetLabels,
              (const std::string&, std::vector<std::string>*),
              (const, override));
  MOCK_METHOD(bool, AddInitialKeyset, (const Credentials&, bool), (override));
  MOCK_METHOD(CryptohomeErrorCode,
              AddKeyset,
              (const Credentials&,
               const brillo::SecureBlob&,
               const KeyData*,
               bool,
               int*),
              (override));
  MOCK_METHOD(CryptohomeErrorCode,
              RemoveKeyset,
              (const Credentials&, const KeyData&),
              (override));
  MOCK_METHOD(bool, ForceRemoveKeyset, (const std::string&, int), (override));
  MOCK_METHOD(bool, MoveKeyset, (const std::string&, int, int), (override));
  MOCK_METHOD(void, RemoveLECredentials, (const std::string&), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_KEYSET_MANAGEMENT_H_
