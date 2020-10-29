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
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/vault_keyset.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::MatchesRegex;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;

namespace cryptohome {

namespace {

struct UserPassword {
  const char* name;
  const char* password;
};

constexpr char kUser0[] = "First User";
constexpr char kUserPassword0[] = "user0_pass";

constexpr char kPasswordLabel[] = "password";

constexpr int kInitialKeysetIndex = 0;

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

  KeyData DefaultKeyData() {
    KeyData key_data;
    key_data.set_label(kPasswordLabel);
    return key_data;
  }

  void KeysetSetUpWithKeyData(const KeyData& key_data) {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&platform_, homedirs_.crypto());
      vk.CreateRandom();
      *vk.mutable_serialized()->mutable_key_data() = key_data;
      user.credentials.set_key_data(key_data);
      ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated));
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }

  void KeysetSetUpWithoutKeyData() {
    for (auto& user : users_) {
      VaultKeyset vk;
      vk.Initialize(&platform_, homedirs_.crypto());
      vk.CreateRandom();
      ASSERT_TRUE(vk.Encrypt(user.passkey, user.obfuscated));
      ASSERT_TRUE(
          vk.Save(user.homedir_path.Append(kKeyFile).AddExtension("0")));
    }
  }
};

// Successfully adds initial keyset
TEST_F(KeysetManagementTest, AddInitialKeyset) {
  // SETUP

  users_[0].credentials.set_key_data(DefaultKeyData());

  // TEST

  EXPECT_TRUE(homedirs_.AddInitialKeyset(users_[0].credentials));

  // VERIFY
  // Initial keyset is added, readable, has "new-er" fields correctly
  // populated and the initial index is "0".

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk0;
  vk0.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk0,
                                       /* error */ nullptr));
  EXPECT_EQ(vk0.legacy_index(), kInitialKeysetIndex);
  EXPECT_EQ(vk0.label(), users_[0].credentials.key_data().label());
  // Expect reset seed and chaps_key to be created.
  EXPECT_TRUE(vk0.serialized().has_wrapped_chaps_key());
  EXPECT_TRUE(vk0.serialized().has_wrapped_reset_seed());
}

// Successfully adds new keyset
TEST_F(KeysetManagementTest, AddKeysetSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, nullptr,
                                false, &index));
  EXPECT_NE(index, -1);

  // VERIFY
  // After we add an additional keyset, we can list and read both of them.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex, index));

  VaultKeyset vk0;
  vk0.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk0,
                                       /* error */ nullptr));
  EXPECT_EQ(vk0.legacy_index(), kInitialKeysetIndex);
  // We don't have reset seed in the initial generation, so make sure it is
  // populated on the original key when we add a new one.
  EXPECT_TRUE(vk0.serialized().has_wrapped_reset_seed());

  VaultKeyset vk1;
  vk1.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(
      homedirs_.GetValidKeyset(new_credentials, &vk1, /* error */ nullptr));
  EXPECT_EQ(vk1.legacy_index(), index);
}

// Overrides existing keyset on label collision when "clobber" flag is present.
TEST_F(KeysetManagementTest, AddKeysetClobberSuccess) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);
  // Re-use key data from existing credentials to cause label collision.
  KeyData key_data = users_[0].credentials.key_data();
  new_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, &key_data,
                                true, &index));
  EXPECT_EQ(index, 0);

  // VERIFY
  // When adding new keyset with an "existing" label and the clobber is on, we
  // expect it to override the keyset with the same label. Thus we shall have
  // a keyset readable with new_credentials under the index of the old keyset.
  // The old keyset shall be removed.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                        /* error */ nullptr));

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
  EXPECT_EQ(vk_new.legacy_index(), kInitialKeysetIndex);
}

// Return error on label collision when no "clobber".
TEST_F(KeysetManagementTest, AddKeysetNoClobber) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);
  // Re-use key data from existing credentials to cause label collision.
  KeyData key_data = users_[0].credentials.key_data();
  new_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_LABEL_EXISTS,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, &key_data,
                                false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Label collision without "clobber" causes an addition error. Old keyset
  // shall still be readable with old credentials, and the new one shall not
  // exist.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to invalid label.
TEST_F(KeysetManagementTest, AddKeysetNonExistentLabel) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  Credentials not_existing_label_credentials = users_[0].credentials;
  KeyData key_data = users_[0].credentials.key_data();
  key_data.set_label("i do not exist");
  not_existing_label_credentials.set_key_data(key_data);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND,
            homedirs_.AddKeyset(not_existing_label_credentials, new_passkey,
                                nullptr, false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Invalid label causes an addition error. Old keyset shall still be
  // readable with old credentials, and the new one shall not  exist.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to invalid credentials.
TEST_F(KeysetManagementTest, AddKeysetInvalidCreds) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  brillo::SecureBlob wrong_passkey("wrong");
  Credentials wrong_credentials(users_[0].name, wrong_passkey);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            homedirs_.AddKeyset(wrong_credentials, new_passkey, nullptr, false,
                                &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Invalid credentials cause an addition error. Old keyset shall still be
  // readable with old credentials, and the new one shall not  exist.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to lacking privilieges.
TEST_F(KeysetManagementTest, AddKeysetInvalidPrivileges) {
  // SETUP

  KeyData vk_key_data;
  vk_key_data.mutable_privileges()->set_add(false);

  KeysetSetUpWithKeyData(vk_key_data);

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, nullptr,
                                false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Invalid permissions cause an addition error. Old keyset shall still be
  // readable with old credentials, and the new one shall not  exist.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to index pool exhaustion.
TEST_F(KeysetManagementTest, AddKeysetNoFreeIndices) {
  // SETUP

  KeysetSetUpWithKeyData(DefaultKeyData());

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  // Use mock not to literally create a hundread files.
  EXPECT_CALL(platform_, OpenFile(Property(&base::FilePath::value,
                                           MatchesRegex(".*/master\\..*$")),
                                  StrEq("wx")))
      .WillRepeatedly(Return(nullptr));

  // TEST

  int index = -1;
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, nullptr,
                                false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // Nothing should change if we were not able to add keyset due to a lack of
  // free slots. Since we mocked the "slot" check, we should still have only
  // initial keyset index, adn the keyset is readable with the old credentials.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to failed encryption.
TEST_F(KeysetManagementTest, AddKeysetEncryptFail) {
  // SETUP

  KeysetSetUpWithoutKeyData();

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  // Mock vk to inject encryption failure
  MockVaultKeysetFactory vault_keyset_factory;
  auto mock_vk = new NiceMock<MockVaultKeyset>();
  mock_vk->mutable_serialized()->set_wrapped_reset_seed("reset_seed");
  EXPECT_CALL(vault_keyset_factory, New(_, _)).WillOnce(Return(mock_vk));
  EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Decrypt(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Encrypt(new_passkey, _)).WillOnce(Return(false));
  homedirs_.set_vault_keyset_factory(&vault_keyset_factory);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, nullptr,
                                false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // If we failed to save the added keyset due to encryption failure, the old
  // keyset should still exist and be readable with the old credentials.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

// Fail to add new keyset due to failed disk write.
TEST_F(KeysetManagementTest, AddKeysetSaveFail) {
  // SETUP

  KeysetSetUpWithoutKeyData();

  brillo::SecureBlob new_passkey("new pass");
  Credentials new_credentials(users_[0].name, new_passkey);

  // Mock vk to inject save failure.
  MockVaultKeysetFactory vault_keyset_factory;
  auto mock_vk = new NiceMock<MockVaultKeyset>();
  mock_vk->mutable_serialized()->set_wrapped_reset_seed("reset_seed");
  EXPECT_CALL(vault_keyset_factory, New(_, _)).WillOnce(Return(mock_vk));
  EXPECT_CALL(*mock_vk, Load(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Decrypt(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Encrypt(new_passkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*mock_vk, Save(_)).WillOnce(Return(false));
  homedirs_.set_vault_keyset_factory(&vault_keyset_factory);

  // TEST

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(users_[0].credentials, new_passkey, nullptr,
                                false, &index));
  EXPECT_EQ(index, -1);

  // VERIFY
  // If we failed to save the added keyset due to disk failure, the old
  // keyset should still exist and be readable with the old credentials.

  std::vector<int> indicies;
  EXPECT_TRUE(homedirs_.GetVaultKeysets(users_[0].obfuscated, &indicies));
  EXPECT_THAT(indicies, ElementsAre(kInitialKeysetIndex));

  VaultKeyset vk_old;
  vk_old.Initialize(&platform_, homedirs_.crypto());
  EXPECT_TRUE(homedirs_.GetValidKeyset(users_[0].credentials, &vk_old,
                                       /* error */ nullptr));
  EXPECT_EQ(vk_old.legacy_index(), kInitialKeysetIndex);

  VaultKeyset vk_new;
  vk_new.Initialize(&platform_, homedirs_.crypto());
  EXPECT_FALSE(
      homedirs_.GetValidKeyset(new_credentials, &vk_new, /* error */ nullptr));
}

}  // namespace cryptohome
