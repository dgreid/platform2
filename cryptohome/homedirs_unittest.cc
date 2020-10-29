// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/homedirs.h"

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_user_oldest_activity_timestamp_cache.h"

using ::testing::_;
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
constexpr char kUser1[] = "Second User";
constexpr char kUserPassword1[] = "user1_pass";
constexpr char kUser2[] = "Third User";
constexpr char kUserPassword2[] = "user2_pass";
constexpr char kOwner[] = "I am the device owner";
constexpr char kOwnerPassword[] = "owner_pass";

constexpr int kOwnerIndex = 3;

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

}  // namespace

class HomeDirsTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  HomeDirsTest() : crypto_(&platform_) {}
  ~HomeDirsTest() override {}

  // Not copyable or movable
  HomeDirsTest(const HomeDirsTest&) = delete;
  HomeDirsTest& operator=(const HomeDirsTest&) = delete;
  HomeDirsTest(HomeDirsTest&&) = delete;
  HomeDirsTest& operator=(HomeDirsTest&&) = delete;

  void SetUp() override {
    PreparePolicy(true, kOwner, false, "");
    crypto_.set_use_tpm(false);
    homedirs_.Init(&platform_, &crypto_, &timestamp_cache_);

    ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt_));
    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);
    AddUser(kUser1, kUserPassword1);
    AddUser(kUser2, kUserPassword2);
    AddUser(kOwner, kOwnerPassword);

    ASSERT_EQ(kOwner, users_[kOwnerIndex].name);

    PrepareDirectoryStructure();
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

  void AddUser(const char* name, const char* password) {
    std::string obfuscated =
        brillo::cryptohome::home::SanitizeUserNameWithSalt(name, system_salt_);
    brillo::SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(password, system_salt_, &passkey);
    Credentials credentials(name, passkey);

    UserInfo info = {name,
                     obfuscated,
                     passkey,
                     credentials,
                     homedirs_.shadow_root().Append(obfuscated),
                     brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PreparePolicy(bool owner_known,
                     const std::string& owner,
                     bool ephemeral_users_enabled,
                     const std::string& clean_up_strategy) {
    policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy, LoadPolicy()).WillRepeatedly(Return(true));
    EXPECT_CALL(*device_policy, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*device_policy, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
    homedirs_.own_policy_provider(new policy::PolicyProvider(
        std::unique_ptr<policy::MockDevicePolicy>(device_policy)));
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

 protected:
  NiceMock<MockPlatform> platform_;
  NiceMock<MockUserOldestActivityTimestampCache> timestamp_cache_;
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

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(homedirs_.shadow_root()));
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
    for (const auto& user : users_) {
      ASSERT_TRUE(platform_.CreateDirectory(user.homedir_path));
      ASSERT_TRUE(
          platform_.CreateDirectory(user.homedir_path.Append(kMountDir)));
      if (ShouldTestEcryptfs()) {
        ASSERT_TRUE(platform_.CreateDirectory(
            user.homedir_path.Append(kEcryptfsVaultDir)));
      }
      ASSERT_TRUE(platform_.CreateDirectory(user.user_path));
    }
  }
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs, HomeDirsTest, ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto, HomeDirsTest, ::testing::Values(false));

TEST_P(HomeDirsTest, RemoveNonOwnerCryptohomes) {
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));

  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  homedirs_.RemoveNonOwnerCryptohomes();

  // Non-owners' vaults are removed
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(users_[2].homedir_path));

  // Owner's vault still exists
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
}

TEST_P(HomeDirsTest, RenameCryptohome) {
  constexpr char kNewUserId[] = "some_new_user";
  const std::string kHashedNewUserId =
      brillo::cryptohome::home::SanitizeUserNameWithSalt(kNewUserId,
                                                         system_salt_);
  const base::FilePath kNewUserPath =
      homedirs_.shadow_root().Append(kHashedNewUserId);

  // Original state - pregenerated users' vaults exist, kNewUserId's vault
  // doesn't exist
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(kNewUserPath));

  // Rename user0
  EXPECT_TRUE(homedirs_.Rename(users_[0].name, kNewUserId));

  // Renamed user0 to kNewUserId, thus user0's vault doesn't exist and
  // kNewUserId's does.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // If source directory doesn't exist, assume renamed.
  EXPECT_TRUE(homedirs_.Rename(users_[0].name, kNewUserId));

  // Since renaming already renamed user, no changes are expected.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // This should fail as target directory already exists.
  EXPECT_FALSE(homedirs_.Rename(users_[1].name, users_[2].name));

  // Since renaming failed, no changes are expected.
  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));

  // Rename back.
  EXPECT_TRUE(homedirs_.Rename(kNewUserId, users_[0].name));

  // Back to the original state - pregenerated users' vaults exist, kNewUserId's
  // vault doesn't exist.
  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[1].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[2].homedir_path));
  EXPECT_TRUE(platform_.DirectoryExists(users_[kOwnerIndex].homedir_path));
  EXPECT_FALSE(platform_.DirectoryExists(kNewUserPath));
}

TEST_P(HomeDirsTest, CreateCryptohome) {
  constexpr char kNewUserId[] = "some_new_user";
  const std::string kHashedNewUserId =
      brillo::cryptohome::home::SanitizeUserNameWithSalt(kNewUserId,
                                                         system_salt_);
  const base::FilePath kNewUserPath =
      homedirs_.shadow_root().Append(kHashedNewUserId);

  EXPECT_TRUE(homedirs_.Create(kNewUserId));
  EXPECT_TRUE(platform_.DirectoryExists(kNewUserPath));
}

}  // namespace cryptohome
