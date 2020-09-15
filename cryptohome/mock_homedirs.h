// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_HOMEDIRS_H_
#define CRYPTOHOME_MOCK_HOMEDIRS_H_

#include "cryptohome/homedirs.h"

#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gmock/gmock.h>

#include "cryptohome/credentials.h"
#include "cryptohome/mount.h"

namespace cryptohome {
class DiskCleanup;
class VaultKeyset;

class MockHomeDirs : public HomeDirs {
 public:
  MockHomeDirs();
  virtual ~MockHomeDirs();

  MOCK_METHOD(bool,
              Init,
              (Platform*, Crypto*, UserOldestActivityTimestampCache*),
              (override));
  MOCK_METHOD(void, RemoveNonOwnerCryptohomes, (), (override));
  MOCK_METHOD(bool, GetOwner, (std::string*), (override));
  MOCK_METHOD(bool, GetPlainOwner, (std::string*), (override));
  MOCK_METHOD(bool, AreEphemeralUsersEnabled, (), (override));
  MOCK_METHOD(bool, AreCredentialsValid, (const Credentials&), (override));
  MOCK_METHOD(bool,
              GetValidKeyset,
              (const Credentials&, VaultKeyset*, int*, MountError*),
              (override));
  MOCK_METHOD(bool, Remove, (const std::string&), (override));
  MOCK_METHOD(bool,
              Rename,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(int64_t, ComputeDiskUsage, (const std::string&), (override));
  MOCK_METHOD(bool,
              Migrate,
              (const Credentials&, const brillo::SecureBlob&, int*),
              (override));
  MOCK_METHOD(bool, Exists, (const std::string&), (const, override));
  MOCK_METHOD(VaultKeyset*,
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
  MOCK_METHOD(CryptohomeErrorCode,
              UpdateKeyset,
              (const Credentials& credentials,
               const Key* changed_data,
               const std::string& authorization_signature),
              (override));
  MOCK_METHOD(bool, ForceRemoveKeyset, (const std::string&, int), (override));
  MOCK_METHOD(bool, MoveKeyset, (const std::string&, int, int), (override));
  MOCK_METHOD(void, RemoveLECredentials, (const std::string&), (override));
  MOCK_METHOD(int32_t, GetUnmountedAndroidDataCount, (), (override));

  MOCK_METHOD(bool,
              NeedsDircryptoMigration,
              (const std::string&),
              (const, override));

  MOCK_METHOD(bool, SetLockedToSingleUser, (), (const, override));
  MOCK_METHOD(std::vector<HomeDir>, GetHomeDirs, (), (override));
  MOCK_METHOD(void,
              AddUserTimestampToCache,
              (const std::string& obfuscated),
              (override));
  MOCK_METHOD(void, set_enterprise_owned, (bool), (override));
  MOCK_METHOD(bool, enterprise_owned, (), (const, override));
  MOCK_METHOD(void, set_shadow_root, (const base::FilePath&), (override));
  MOCK_METHOD(const base::FilePath&, shadow_root, (), (const, override));
  MOCK_METHOD(void, set_disk_cleanup, (DiskCleanup*), (override));
  MOCK_METHOD(DiskCleanup*, disk_cleanup, (), (const, override));

  // Some unit tests require that MockHomeDirs actually call the real
  // GetPlainOwner() function. In those cases, you can use this function
  // to forward the mocked GetOwner() to ActualGetOwner().
  bool ActualGetPlainOwner(std::string* owner);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_HOMEDIRS_H_
