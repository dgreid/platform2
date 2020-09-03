// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_MOUNT_H_
#define CRYPTOHOME_MOCK_MOUNT_H_

#include "cryptohome/mount.h"

#include <string>

#include <base/files/file_path.h>
#include <gmock/gmock.h>

namespace cryptohome {
class Credentials;

class MockMount : public Mount {
 public:
  MockMount();
  ~MockMount();
  MOCK_METHOD(bool,
              Init,
              (Platform*,
               Crypto*,
               UserOldestActivityTimestampCache*),
              (override));
  MOCK_METHOD(bool, AreSameUser, (const std::string&), (override));
  MOCK_METHOD(const UserSession*, GetCurrentUserSession, (), (const, override));
  MOCK_METHOD(bool, AreValid, (const Credentials&), (override));
  MOCK_METHOD(bool,
              MountCryptohome,
              (const Credentials&, const Mount::MountArgs&, MountError*),
              (override));
  MOCK_METHOD(bool, UnmountCryptohome, (), (override));
  MOCK_METHOD(bool, IsMounted, (), (const, override));
  MOCK_METHOD(bool, IsNonEphemeralMounted, (), (const, override));
  MOCK_METHOD(bool, MountGuestCryptohome, (), (override));
  MOCK_METHOD(bool,
              UpdateCurrentUserActivityTimestamp,
              (int),
              (override));  // NOLINT
  MOCK_METHOD(const base::FilePath&, mount_point, (), (const, override));
  MOCK_METHOD(bool, OwnsMountPoint, (const base::FilePath&), (const, override));
  MOCK_METHOD(bool, InsertPkcs11Token, (), (override));
  MOCK_METHOD(void, RemovePkcs11Token, (), (override));
  MOCK_METHOD(Pkcs11State, pkcs11_state, (), (override));

  Pkcs11State Real_pkcs11_state() { return Mount::pkcs11_state(); }

  MOCK_METHOD(
      bool,
      MigrateToDircrypto,
      (const dircrypto_data_migrator::MigrationHelper::ProgressCallback&,
       MigrationType),
      (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_MOUNT_H_
