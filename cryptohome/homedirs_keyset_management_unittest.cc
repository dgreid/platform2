// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/homedirs.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/vault_keyset.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

namespace {

struct UserPassword {
  const char* name;
  const char* password;
};

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";

constexpr char kPasswordLabel[] = "password";

}  // namespace

// TODO(dlunev): Remove kKeyFile extern declaration once we have it declared
// in the proper place.
extern const char kKeyFile[];

class KeysetManagementTest : public ::testing::Test {
 public:
  KeysetManagementTest() : crypto_(&platform_) {}
  ~KeysetManagementTest() override {}

  // Not copyable or movable
  KeysetManagementTest(const KeysetManagementTest&) = delete;
  KeysetManagementTest& operator=(const KeysetManagementTest&) = delete;
  KeysetManagementTest(KeysetManagementTest&&) = delete;
  KeysetManagementTest& operator=(KeysetManagementTest&&) = delete;

  void SetUp() override {
    crypto_.set_tpm(&tpm_);
    crypto_.set_use_tpm(false);
    homedirs_.Init(&platform_, &crypto_, nullptr);

    ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt_));
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);

    PrepareDirectoryStructure();
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

 protected:
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  Crypto crypto_;
  HomeDirs homedirs_;
  brillo::SecureBlob system_salt_;

  struct UserInfo {
    std::string name;
    std::string obfuscated;
    brillo::SecureBlob passkey;
    Credentials credentials;
    base::FilePath homedir_path;
    base::FilePath user_path;
  };

  // Information about users' homedirs. The order of users is equal to kUsers.
  std::vector<UserInfo> users_;

  void AddUser(const char* name, const char* password) {
    std::string obfuscated =
        brillo::cryptohome::home::SanitizeUserNameWithSalt(name, system_salt_);
    brillo::SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(password, system_salt_, &passkey);
    Credentials credentials(name, passkey);
    KeyData key_data;
    key_data.set_label(kPasswordLabel);
    credentials.set_key_data(key_data);

    UserInfo info = {name,
                     obfuscated,
                     passkey,
                     credentials,
                     homedirs_.shadow_root().Append(obfuscated),
                     brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(homedirs_.shadow_root()));
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
    // We only need the homedir path, not the vault/mount paths.
    for (const auto& user : users_) {
      ASSERT_TRUE(platform_.CreateDirectory(user.homedir_path));
    }
  }
};

// Successfully adds initial keyset
TEST_F(KeysetManagementTest, AddInitialKeyset) {
  // No key setup to test addition of the first keyset.

  // TEST

  EXPECT_TRUE(homedirs_.AddInitialKeyset(users_[0].credentials));

  // VERIFY

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(0));

  VaultKeyset vk0;
  vk0.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk0,
                                       /* error */ nullptr));
  EXPECT_EQ(vk0.legacy_index(), 0);
  EXPECT_EQ(vk0.label(), users_[0].credentials.key_data().label());
  // Expect reset seed and chaps_key to be created.
  EXPECT_TRUE(vk0.serialized().has_wrapped_chaps_key());
  EXPECT_TRUE(vk0.serialized().has_wrapped_reset_seed());
}

}  // namespace cryptohome
