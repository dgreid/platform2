// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Creates credential stores for testing

#include "cryptohome/make_tests.h"

#include <openssl/evp.h>
#include <stdint.h>

#include <algorithm>
#include <memory>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mount.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::AnyOf;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Property;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;

namespace cryptohome {

// struct TestUserInfo {
//   const char* username;
//   const char* password;
//   bool create;
// };

const TestUserInfo kDefaultUsers[] = {
    {"testuser0@invalid.domain", "zero", true, false},
    {"testuser1@invalid.domain", "one", true, false},
    {"testuser2@invalid.domain", "two", true, false},
    {"testuser3@invalid.domain", "three", true, false},
    {"testuser4@invalid.domain", "four", true, false},
    {"testuser5@invalid.domain", "five", false, false},
    {"testuser6@invalid.domain", "six", true, false},
    {"testuser7@invalid.domain", "seven", true, false},
    {"testuser8@invalid.domain", "eight", true, false},
    {"testuser9@invalid.domain", "nine", true, false},
    {"testuser10@invalid.domain", "ten", true, false},
    {"testuser11@invalid.domain", "eleven", true, false},
    {"testuser12@invalid.domain", "twelve", false, false},
    {"testuser13@invalid.domain", "thirteen", true, false},
    {"testuser14@invalid.domain", "0014", true, true},
};
const size_t kDefaultUserCount = base::size(kDefaultUsers);

MakeTests::MakeTests() {}

void MakeTests::InitTestData(const FilePath& image_dir,
                             const TestUserInfo* test_users,
                             size_t test_user_count,
                             bool force_ecryptfs) {
  CHECK(system_salt.size()) << "Call SetUpSystemSalt() first";
  users.clear();
  users.resize(test_user_count);
  const TestUserInfo* user_info = test_users;
  for (size_t id = 0; id < test_user_count; ++id, ++user_info) {
    TestUser* user = &users[id];
    user->FromInfo(user_info, image_dir);
    user->GenerateCredentials(force_ecryptfs);
  }
}

void MakeTests::SetUpSystemSalt() {
  std::string* salt = new std::string(CRYPTOHOME_DEFAULT_SALT_LENGTH, 'A');
  system_salt.resize(salt->size());
  memcpy(&system_salt[0], salt->data(), salt->size());
  brillo::cryptohome::home::SetSystemSalt(salt);
}

void MakeTests::TearDownSystemSalt() {
  std::string* salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::cryptohome::home::SetSystemSalt(NULL);
  delete salt;
}

void MakeTests::InjectSystemSalt(MockPlatform* platform, const FilePath& path) {
  CHECK(brillo::cryptohome::home::GetSystemSalt());
  EXPECT_CALL(*platform, FileExists(path)).WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, GetFileSize(path, _))
      .WillRepeatedly(
          DoAll(SetArgPointee<1>(system_salt.size()), Return(true)));

  EXPECT_CALL(*platform, ReadFileToSecureBlob(path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(system_salt), Return(true)));
}

void MakeTests::InjectEphemeralSkeleton(MockPlatform* platform,
                                        const FilePath& root) {
  EXPECT_CALL(*platform,
              SetOwnership(Property(&FilePath::value, StartsWith(root.value())),
                           _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, DirectoryExists(Property(&FilePath::value,
                                                  StartsWith(root.value()))))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*platform,
              FileExists(Property(&FilePath::value, StartsWith(root.value()))))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(*platform,
              SetGroupAccessible(
                  Property(&FilePath::value, StartsWith(root.value())), _, _))
      .WillRepeatedly(Return(true));
}

void TestUser::FromInfo(const struct TestUserInfo* info,
                        const FilePath& image_dir) {
  username = info->username;
  password = info->password;
  create = info->create;
  is_le_credential = info->is_le_credential;
  use_key_data = is_le_credential ? true : false;
  // Stub system salt must already be in place. See MountTest::SetUp().
  // Sanitized usernames and obfuscated ones differ by case. Accomodate both.
  // TODO(ellyjones) fix this discrepancy!
  sanitized_username = brillo::cryptohome::home::SanitizeUserName(username);
  obfuscated_username = sanitized_username;
  std::transform(obfuscated_username.begin(), obfuscated_username.end(),
                 obfuscated_username.begin(), ::tolower);
  // Both pass this check though.
  DCHECK(brillo::cryptohome::home::IsSanitizedUserName(obfuscated_username));
  shadow_root = image_dir;
  skel_dir = image_dir.Append("skel");
  base_path = image_dir.Append(obfuscated_username);
  vault_path = base_path.Append("vault");
  vault_mount_path = base_path.Append("mount");
  ephemeral_mount_path = FilePath(kEphemeralCryptohomeDir)
                             .Append("ephemeral_mount")
                             .Append(obfuscated_username);
  tracked_directories_json_path = base_path.Append("tracked_directories.json");
  root_vault_path = vault_path.Append("root");
  user_vault_path = vault_path.Append("user");
  root_vault_mount_path = vault_mount_path.Append("root");
  user_vault_mount_path = vault_mount_path.Append("user");
  root_ephemeral_mount_path = ephemeral_mount_path.Append("root");
  user_ephemeral_mount_path = ephemeral_mount_path.Append("user");
  keyset_path = base_path.Append("master.0");
  timestamp_path = base_path.Append("master.0.timestamp");
  mount_prefix = brillo::cryptohome::home::GetUserPathPrefix().DirName();
  legacy_user_mount_path = FilePath("/home/chronos/user");
  user_mount_path =
      brillo::cryptohome::home::GetUserPath(username).StripTrailingSeparators();
  user_mount_prefix =
      brillo::cryptohome::home::GetUserPathPrefix().StripTrailingSeparators();
  root_mount_path =
      brillo::cryptohome::home::GetRootPath(username).StripTrailingSeparators();
  root_mount_prefix =
      brillo::cryptohome::home::GetRootPathPrefix().StripTrailingSeparators();
}

void TestUser::GenerateCredentials(bool force_ecryptfs) {
  std::string* system_salt = brillo::cryptohome::home::GetSystemSalt();
  brillo::Blob salt(system_salt->begin(), system_salt->end());
  SecureBlob sec_salt(*system_salt);
  NiceMock<MockTpm> tpm;
  NiceMock<MockPlatform> platform;
  Crypto crypto(&platform);
  crypto.set_use_tpm(false);
  crypto.set_disable_logging_for_testing(/*disable=*/true);
  CryptoLib::SetScryptTestingParams();
  UserOldestActivityTimestampCache timestamp_cache;

  scoped_refptr<Mount> mount = new Mount();
  mount->set_shadow_root(shadow_root);
  mount->set_skel_source(skel_dir);
  mount->set_use_tpm(false);
  NiceMock<policy::MockDevicePolicy>* device_policy =
      new NiceMock<policy::MockDevicePolicy>;
  EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
  mount->set_policy_provider(new policy::PolicyProvider(
      std::unique_ptr<NiceMock<policy::MockDevicePolicy>>(device_policy)));
  FilePath keyset_path =
      shadow_root.Append(obfuscated_username).Append("master.0");
  EXPECT_CALL(platform, FileExists(keyset_path)).WillOnce(Return(false));
  FilePath salt_path = shadow_root.Append("salt");
  int64_t salt_size = salt.size();
  EXPECT_CALL(platform, FileExists(salt_path)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform, GetFileSize(salt_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(salt_size), Return(true)));
  EXPECT_CALL(platform, ReadFile(salt_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(salt), Return(true)));
  EXPECT_CALL(platform, ReadFileToSecureBlob(salt_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(sec_salt), Return(true)));
  EXPECT_CALL(platform, DirectoryExists(shadow_root))
      .WillRepeatedly(Return(true));
  platform.GetFake()->SetStandardUsersAndGroups();
  mount->Init(&platform, &crypto, &timestamp_cache);

  cryptohome::Crypto::PasswordToPasskey(password, sec_salt, &passkey);
  Credentials local_credentials(username, passkey);
  if (use_key_data) {
    if (is_le_credential)
      key_data.set_label("PIN");
    local_credentials.set_key_data(key_data);
  }
  // NOTE! This code gives us generated credentials for credentials tests NOT
  // NOTE! golden credentials to test against.  This means we won't see problems
  // NOTE! if the credentials generation and checking code break together.
  // TODO(wad,ellyjones) Add golden credential tests too.

  // Use 'stat' failures to trigger default-allow the creation of the paths.
  EXPECT_CALL(
      platform,
      Stat(Property(
               &FilePath::value,
               AnyOf("/home", "/home/root",
                     brillo::cryptohome::home::GetRootPath(username).value(),
                     "/home/user",
                     brillo::cryptohome::home::GetUserPath(username).value())),
           _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(
      platform,
      Stat(Property(&FilePath::value,
                    AnyOf("/home/chronos",
                          MountHelper::GetNewUserPath(username).value())),
           _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform, CreateDirectory(_)).WillRepeatedly(Return(true));
  // Grab the generated credential
  EXPECT_CALL(platform, WriteFileAtomicDurable(keyset_path, _, _))
      .WillOnce(DoAll(SaveArg<1>(&credentials), Return(true)));
  EXPECT_TRUE(mount->CreateCryptohome(local_credentials, force_ecryptfs));
  DCHECK(credentials.size());

  // Unmount succeeds. This is called when |mount| is destroyed.
  ON_CALL(platform, Unmount(_, _, _)).WillByDefault(Return(true));
}

void TestUser::InjectKeyset(MockPlatform* platform, bool enumerate) {
  // TODO(wad) Update to support multiple keys
  EXPECT_CALL(*platform, FileExists(Property(&FilePath::value,
                                             StartsWith(keyset_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, ReadFile(keyset_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(credentials), Return(true)));
  if (enumerate) {
    EXPECT_CALL(*platform, GetFileEnumerator(base_path, false, _))
        .WillRepeatedly(Invoke([&](base::FilePath path, bool rec, int type) {
          MockFileEnumerator* files = new NiceMock<MockFileEnumerator>();
          // Single key.
          files->AddFileEntry(keyset_path);
          return files;
        }));
  }
}

void TestUser::InjectUserPaths(MockPlatform* platform,
                               uid_t chronos_uid,
                               gid_t chronos_gid,
                               gid_t chronos_access_gid,
                               gid_t daemon_gid,
                               bool is_ecryptfs) {
  scoped_refptr<Mount> temp_mount = new Mount();
  base::stat_wrapper_t root_dir;
  memset(&root_dir, 0, sizeof(root_dir));
  root_dir.st_mode = S_IFDIR | S_ISVTX;
  EXPECT_CALL(*platform,
              Stat(AnyOf(mount_prefix, root_mount_prefix, user_mount_prefix,
                         root_mount_path, user_vault_path),
                   _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(root_dir), Return(true)));
  // Avoid triggering vault migration.  (Is there another test for that?)
  base::stat_wrapper_t root_vault_dir;
  memset(&root_vault_dir, 0, sizeof(root_vault_dir));
  root_vault_dir.st_mode = S_IFDIR | S_ISVTX;
  root_vault_dir.st_uid = 0;
  root_vault_dir.st_gid = daemon_gid;
  EXPECT_CALL(*platform,
              Stat(is_ecryptfs ? root_vault_path : root_vault_mount_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(root_vault_dir), Return(true)));
  base::stat_wrapper_t user_dir;
  memset(&user_dir, 0, sizeof(user_dir));
  user_dir.st_mode = S_IFDIR;
  user_dir.st_uid = chronos_uid;
  user_dir.st_gid = chronos_access_gid;
  EXPECT_CALL(
      *platform,
      Stat(AnyOf(user_mount_path, MountHelper::GetNewUserPath(username)), _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(user_dir), Return(true)));
  if (!is_ecryptfs) {
    EXPECT_CALL(*platform,
                Stat(Property(&FilePath::value,
                              StartsWith(user_vault_mount_path.value())),
                     _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(user_dir), Return(true)));
  }
  base::stat_wrapper_t chronos_dir;
  memset(&chronos_dir, 0, sizeof(chronos_dir));
  chronos_dir.st_mode = S_IFDIR;
  chronos_dir.st_uid = chronos_uid;
  chronos_dir.st_gid = chronos_gid;
  EXPECT_CALL(*platform, Stat(FilePath("/home/chronos"), _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(chronos_dir), Return(true)));
  EXPECT_CALL(*platform, DirectoryExists(Property(
                             &FilePath::value,
                             AnyOf(shadow_root.value(), mount_prefix.value(),
                                   StartsWith(legacy_user_mount_path.value()),
                                   StartsWith(vault_mount_path.value())))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform, DirectoryExists(Property(
                             &FilePath::value, StartsWith(vault_path.value()))))
      .WillRepeatedly(Return(is_ecryptfs));
  FilePath new_user_path = MountHelper::GetNewUserPath(username);
  EXPECT_CALL(*platform, DirectoryExists(Property(
                             &FilePath::value,
                             AnyOf(StartsWith(legacy_user_mount_path.value()),
                                   StartsWith(vault_mount_path.value()),
                                   StartsWith(user_mount_path.value()),
                                   StartsWith(root_mount_path.value()),
                                   StartsWith(new_user_path.value()),
                                   StartsWith(keyset_path.value())))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(*platform,
              SetGroupAccessible(
                  Property(&FilePath::value,
                           AnyOf(StartsWith(legacy_user_mount_path.value()),
                                 StartsWith(user_vault_mount_path.value()))),
                  chronos_access_gid, _))
      .WillRepeatedly(Return(true));
  if (!is_ecryptfs) {
    EXPECT_CALL(*platform, GetDirCryptoKeyState(vault_mount_path))
        .WillRepeatedly(Return(dircrypto::KeyState::ENCRYPTED));
  }
}

}  // namespace cryptohome
