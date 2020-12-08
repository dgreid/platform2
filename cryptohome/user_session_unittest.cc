// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for UserSession.

#include "cryptohome/user_session.h"

#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/keyset_management.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mock_mount.h"

using brillo::SecureBlob;

using ::testing::_;
using ::testing::ByRef;
using ::testing::NiceMock;
using ::testing::Return;

namespace cryptohome {

namespace {

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";

}  // namespace

class UserSessionTest : public ::testing::Test {
 public:
  UserSessionTest() : crypto_(&platform_) {}
  ~UserSessionTest() override {}

  // Not copyable or movable
  UserSessionTest(const UserSessionTest&) = delete;
  UserSessionTest& operator=(const UserSessionTest&) = delete;
  UserSessionTest(UserSessionTest&&) = delete;
  UserSessionTest& operator=(UserSessionTest&&) = delete;

  void SetUp() override {
    InitializeFilesystemLayout(&platform_, &crypto_, &system_salt_);
    keyset_management_ = std::make_unique<KeysetManagement>(
        &platform_, &crypto_, system_salt_,
        std::make_unique<VaultKeysetFactory>());
    homedirs_ = std::make_unique<HomeDirs>(
        &platform_, keyset_management_.get(), system_salt_, nullptr,
        std::make_unique<policy::PolicyProvider>());

    platform_.GetFake()->SetSystemSaltForLibbrillo(system_salt_);

    AddUser(kUser0, kUserPassword0);

    PrepareDirectoryStructure();

    mount_ = new NiceMock<MockMount>();
    session_ = new UserSession(homedirs_.get(), system_salt_, mount_);
  }

  void TearDown() override {
    platform_.GetFake()->RemoveSystemSaltForLibbrillo();
  }

 protected:
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
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  brillo::SecureBlob system_salt_;
  std::unique_ptr<KeysetManagement> keyset_management_;
  std::unique_ptr<HomeDirs> homedirs_;
  scoped_refptr<UserSession> session_;
  // TODO(dlunev): Replace with real mount when FakePlatform is mature enough
  // to support it mock-less.
  scoped_refptr<MockMount> mount_;

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
                     ShadowRoot().Append(obfuscated),
                     brillo::cryptohome::home::GetHashedUserPath(obfuscated)};
    users_.push_back(info);
  }

  void PrepareDirectoryStructure() {
    ASSERT_TRUE(platform_.CreateDirectory(ShadowRoot()));
    ASSERT_TRUE(platform_.CreateDirectory(
        brillo::cryptohome::home::GetUserPathPrefix()));
  }
};

MATCHER_P(MountArgsEqual, mount_args, "") {
  return memcmp(&mount_args, &arg, sizeof(mount_args)) == 0;
}

// Mount twice: first time with create, and the second time for the existing
// one.
TEST_F(UserSessionTest, MountVaultOk) {
  // SETUP

  constexpr int64_t kTs1 = 42;
  constexpr int64_t kTs2 = 43;
  constexpr int64_t kTs3 = 44;

  Mount::MountArgs mount_args_create;
  // Test with ecryptfs since it has a simpler existence check.
  mount_args_create.create_as_ecryptfs = true;
  mount_args_create.create_if_missing = true;

  EXPECT_CALL(*mount_, PrepareCryptohome(users_[0].obfuscated, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_,
              MountCryptohome(users_[0].name, _,
                              MountArgsEqual(mount_args_create), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime())
      .Times(2)  // Initial set and update on mount.
      .WillRepeatedly(Return(base::Time::FromInternalValue(kTs1)));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, mount_args_create));

  // VERIFY
  // Vault created.

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  std::unique_ptr<VaultKeyset> vk0 =
      keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts1 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts1, kTs1);

  // SETUP

  // TODO(dlunev): this is required to mimic a real Mount::PrepareCryptohome
  // call. Remove it when we are not mocking mount.
  platform_.CreateDirectory(GetEcryptfsUserVaultPath(users_[0].obfuscated));

  Mount::MountArgs mount_args_no_create;
  mount_args_no_create.create_if_missing = false;

  EXPECT_CALL(*mount_,
              MountCryptohome(users_[0].name, _,
                              MountArgsEqual(mount_args_no_create), false, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::FromInternalValue(kTs2)));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, mount_args_no_create));

  // VERIFY
  // Vault still exists when tried to remount with no create.
  // ts updated on mount

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  vk0 = keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts2 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts2, kTs2);

  // SETUP

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::FromInternalValue(kTs3)));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  // TEST

  ASSERT_TRUE(session_->Unmount());

  // VERIFY
  // ts updated on unmount

  vk0 = keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts3 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts3, kTs3);
}

TEST_F(UserSessionTest, MountVaultWrongCreds) {
  // SETUP

  constexpr int64_t kTs1 = 42;

  Mount::MountArgs mount_args_create;
  // Test with ecryptfs since it has a simpler existence check.
  mount_args_create.create_as_ecryptfs = true;
  mount_args_create.create_if_missing = true;

  EXPECT_CALL(*mount_, PrepareCryptohome(users_[0].obfuscated, true))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_,
              MountCryptohome(users_[0].name, _,
                              MountArgsEqual(mount_args_create), true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_CALL(platform_, GetCurrentTime())
      .Times(2)  // Initial set and update on mount.
      .WillRepeatedly(Return(base::Time::FromInternalValue(kTs1)));

  ASSERT_EQ(MOUNT_ERROR_NONE,
            session_->MountVault(users_[0].credentials, mount_args_create));

  std::unique_ptr<VaultKeyset> vk0 =
      keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts1 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts1, kTs1);

  // TODO(dlunev): this is required to mimic a real Mount::PrepareCryptohome
  // call. Remove it when we are not mocking mount.
  platform_.CreateDirectory(GetEcryptfsUserVaultPath(users_[0].obfuscated));

  Mount::MountArgs mount_args_no_create;
  mount_args_no_create.create_if_missing = false;

  EXPECT_CALL(*mount_,
              MountCryptohome(users_[0].name, _,
                              MountArgsEqual(mount_args_no_create), false, _))
      .Times(0);

  Credentials wrong_creds(users_[0].name, brillo::SecureBlob("wrong"));

  // TEST

  ASSERT_EQ(MOUNT_ERROR_KEY_FAILURE,
            session_->MountVault(wrong_creds, mount_args_no_create));

  // VERIFY
  // Failed to remount with wrong credentials.

  EXPECT_TRUE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_TRUE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_TRUE(keyset_management_->AreCredentialsValid(users_[0].credentials));

  // No mount, no ts update.
  vk0 = keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts2 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts2, ts1);

  // SETUP

  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(false));
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));

  // TEST

  ASSERT_TRUE(session_->Unmount());

  // VERIFY
  // No unmount, no ts update.

  vk0 = keyset_management_->LoadVaultKeysetForUser(users_[0].obfuscated, 0);
  const int64_t ts3 = vk0->serialized().last_activity_timestamp();
  EXPECT_EQ(ts3, ts2);
}

// Fail to mount because vault doesn't exist and creation is disaalowed.
TEST_F(UserSessionTest, MountVaultNoExistNoCreate) {
  // SETUP

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = false;

  // TEST

  ASSERT_EQ(MOUNT_ERROR_USER_DOES_NOT_EXIST,
            session_->MountVault(users_[0].credentials, mount_args));

  // VERIFY

  EXPECT_FALSE(platform_.DirectoryExists(users_[0].homedir_path));
  EXPECT_FALSE(session_->VerifyCredentials(users_[0].credentials));
  EXPECT_FALSE(keyset_management_->AreCredentialsValid(users_[0].credentials));
}

class UserSessionReAuthTest : public ::testing::Test {
 public:
  UserSessionReAuthTest() : salt() {}
  virtual ~UserSessionReAuthTest() {}

  // Not copyable or movable
  UserSessionReAuthTest(const UserSessionReAuthTest&) = delete;
  UserSessionReAuthTest& operator=(const UserSessionReAuthTest&) = delete;
  UserSessionReAuthTest(UserSessionReAuthTest&&) = delete;
  UserSessionReAuthTest& operator=(UserSessionReAuthTest&&) = delete;

  void SetUp() {
    salt.resize(16);
    CryptoLib::GetSecureRandom(salt.data(), salt.size());
  }

 protected:
  SecureBlob salt;
};

TEST_F(UserSessionReAuthTest, VerifyUser) {
  Credentials credentials("username", SecureBlob("password"));
  scoped_refptr<UserSession> session = new UserSession(nullptr, salt, nullptr);
  EXPECT_TRUE(session->SetCredentials(credentials, 0));

  EXPECT_TRUE(session->VerifyUser(credentials.GetObfuscatedUsername(salt)));
  EXPECT_FALSE(session->VerifyUser("other"));
}

TEST_F(UserSessionReAuthTest, VerifyCredentials) {
  Credentials credentials_1("username", SecureBlob("password"));
  Credentials credentials_2("username", SecureBlob("password2"));
  Credentials credentials_3("username2", SecureBlob("password2"));

  scoped_refptr<UserSession> session = new UserSession(nullptr, salt, nullptr);
  EXPECT_TRUE(session->SetCredentials(credentials_1, 0));
  EXPECT_TRUE(session->VerifyCredentials(credentials_1));
  EXPECT_FALSE(session->VerifyCredentials(credentials_2));
  EXPECT_FALSE(session->VerifyCredentials(credentials_3));

  EXPECT_TRUE(session->SetCredentials(credentials_2, 0));
  EXPECT_FALSE(session->VerifyCredentials(credentials_1));
  EXPECT_TRUE(session->VerifyCredentials(credentials_2));
  EXPECT_FALSE(session->VerifyCredentials(credentials_3));

  EXPECT_TRUE(session->SetCredentials(credentials_3, 0));
  EXPECT_FALSE(session->VerifyCredentials(credentials_1));
  EXPECT_FALSE(session->VerifyCredentials(credentials_2));
  EXPECT_TRUE(session->VerifyCredentials(credentials_3));
}

}  // namespace cryptohome
