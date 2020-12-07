// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_HOMEDIRS_H_
#define CRYPTOHOME_MOCK_HOMEDIRS_H_

#include "cryptohome/homedirs.h"

#include <memory>
#include <string>
#include <vector>

#include <brillo/secure_blob.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <gmock/gmock.h>

#include "cryptohome/credentials.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mount.h"

namespace cryptohome {
class DiskCleanup;
class VaultKeyset;

class MockHomeDirs : public HomeDirs {
 public:
  MockHomeDirs() = default;
  virtual ~MockHomeDirs() = default;

  MOCK_METHOD(void, RemoveNonOwnerCryptohomes, (), (override));
  MOCK_METHOD(bool, GetOwner, (std::string*), (override));
  MOCK_METHOD(bool, GetPlainOwner, (std::string*), (override));
  MOCK_METHOD(bool, AreEphemeralUsersEnabled, (), (override));
  MOCK_METHOD(bool, Create, (const std::string&), (override));
  MOCK_METHOD(bool, Remove, (const std::string&), (override));
  MOCK_METHOD(bool,
              Rename,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(int64_t, ComputeDiskUsage, (const std::string&), (override));
  MOCK_METHOD(bool, Exists, (const std::string&), (const, override));
  MOCK_METHOD(bool, CryptohomeExists, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              UpdateActivityTimestamp,
              (const std::string&, int, int),
              (override));
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
  MOCK_METHOD(KeysetManagement*, keyset_management, (), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_HOMEDIRS_H_
