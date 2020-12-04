// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Creates credential stores for testing.  This class is only used in prepping
// the test data for unit tests.

#ifndef CRYPTOHOME_MAKE_TESTS_H_
#define CRYPTOHOME_MAKE_TESTS_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

struct TestUserInfo {
  const char* username;
  const char* password;
  bool create;
  bool is_le_credential;
};

extern const TestUserInfo kDefaultUsers[];
extern const size_t kDefaultUserCount;
class TestUser;

class MakeTests {
 public:
  MakeTests();
  MakeTests(const MakeTests&) = delete;
  MakeTests& operator=(const MakeTests&) = delete;

  virtual ~MakeTests() {}

  void SetUpSystemSalt();
  void InitTestData(const TestUserInfo* test_users,
                    size_t test_user_count,
                    bool force_ecryptfs);
  void InjectSystemSalt(MockPlatform* platform);
  // Inject mocks needed for skeleton population.
  void InjectEphemeralSkeleton(MockPlatform* platform,
                               const base::FilePath& root);
  void TearDownSystemSalt();

  std::vector<TestUser> users;
  brillo::SecureBlob system_salt;
};

class TestUser {
 public:
  TestUser() {}
  virtual ~TestUser() {}
  // Populate from struct TestUserInfo.
  void FromInfo(const struct TestUserInfo* info);
  // Generate a valid vault keyset using scrypt.
  void GenerateCredentials(bool force_ecryptfs);
  // Inject the keyset so it can be accessed via platform.
  void InjectKeyset(MockPlatform* platform, bool enumerate = true);
  // Inject all the paths for a vault to exist.
  void InjectUserPaths(MockPlatform* platform,
                       uid_t chronos_uid,
                       gid_t chronos_gid,
                       gid_t chronos_access_gid,
                       gid_t daemon_gid,
                       bool is_ecryptfs);
  // Completely public accessors like the TestUserInfo struct.
  const char* username;
  const char* password;
  bool create;
  bool is_le_credential;
  std::string obfuscated_username;
  std::string sanitized_username;
  base::FilePath base_path;
  base::FilePath vault_path;
  base::FilePath vault_mount_path;
  base::FilePath ephemeral_mount_path;
  base::FilePath tracked_directories_json_path;
  base::FilePath user_vault_path;
  base::FilePath root_vault_path;
  base::FilePath user_vault_mount_path;
  base::FilePath root_vault_mount_path;
  base::FilePath user_ephemeral_mount_path;
  base::FilePath root_ephemeral_mount_path;
  base::FilePath keyset_path;
  base::FilePath timestamp_path;
  base::FilePath mount_prefix;
  base::FilePath legacy_user_mount_path;
  base::FilePath user_mount_path;
  base::FilePath root_mount_path;
  base::FilePath user_mount_prefix;
  base::FilePath root_mount_prefix;
  brillo::Blob credentials;
  brillo::SecureBlob passkey;
  bool use_key_data;
  KeyData key_data;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MAKE_TESTS_H_
