// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(dlunev): remove the file when then new implementation of the tests is
// ready.

#include "cryptohome/homedirs.h"

#include <memory>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/data_encoding.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_user_oldest_activity_timestamp_cache.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/mock_vault_keyset_factory.h"
#include "cryptohome/mount.h"
#include "cryptohome/signed_secret.pb.h"

using base::FilePath;
using base::StringPrintf;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::MatchesRegex;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::SetArrayArgument;
using ::testing::StartsWith;
using ::testing::StrEq;

namespace cryptohome {

extern const char kKeyFile[];
extern const int kKeyFileMax;
extern const char kKeyLegacyPrefix[];

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

ACTION_P(SetCleanUpStrategy, clean_up_strategy) {
  if (!clean_up_strategy.empty()) {
    *arg0 = clean_up_strategy;
    return true;
  }
  return false;
}

namespace {
const FilePath kTestRoot("/home/.shadow");

struct homedir {
  const char* name;
  base::Time::Exploded time;
};

const char* kOwner = "<<OWNER>>";
// Note, the order is important. These should be oldest to newest.
const struct homedir kHomedirs[] = {
    {"d5510a8dda6d743c46dadd979a61ae5603529742", {2011, 1, 6, 1}},
    {"8f995cdee8f0711fd32e1cf6246424002c483d47", {2011, 2, 2, 1}},
    {"973b9640e86f6073c6b6e2759ff3cf3084515e61", {2011, 3, 2, 1}},
    {kOwner, {2011, 4, 5, 1}}};

}  // namespace

class OldHomeDirsTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  OldHomeDirsTest() : crypto_(&platform_) {}
  virtual ~OldHomeDirsTest() {}

  void SetUp() {
    test_helper_.SetUpSystemSalt();
    // TODO(wad) Only generate the user data we need. This is time consuming.
    test_helper_.InitTestData(kTestRoot, kDefaultUsers, kDefaultUserCount,
                              ShouldTestEcryptfs());
    homedirs_.set_shadow_root(kTestRoot);
    test_helper_.InjectSystemSalt(&platform_, kTestRoot.Append("salt"));
    set_policy(true, kOwner, false, "");

    homedirs_.Init(&platform_, &crypto_, &timestamp_cache_);
    FilePath fp = FilePath(kTestRoot);
    for (const auto& hd : kHomedirs) {
      FilePath path = fp.Append(hd.name);
      std::string user;
      if (hd.name == std::string(kOwner))
        homedirs_.GetOwner(&user);
      else
        user = hd.name;
      obfuscated_users_.push_back(user);
      homedir_paths_.push_back(fp.Append(user));
      user_paths_.push_back(brillo::cryptohome::home::GetHashedUserPath(user));
      base::Time t;
      CHECK(base::Time::FromUTCExploded(hd.time, &t));
      homedir_times_.push_back(t);
    }
    EXPECT_CALL(platform_, HasExtendedFileAttribute(_, kRemovableFileAttribute))
        .WillRepeatedly(Return(false));
  }

  void TearDown() { test_helper_.TearDownSystemSalt(); }

  void set_policy(bool owner_known,
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

  // Create an enumerator that will enumerate the given child_directories.
  NiceMock<MockFileEnumerator>* CreateFileEnumerator(
      const std::vector<FilePath>& child_directories) {
    auto* mock = new NiceMock<MockFileEnumerator>;
    for (const auto& child : child_directories) {
      base::stat_wrapper_t stat = {};
      mock->entries_.push_back(FileEnumerator::FileInfo(child, stat));
    }
    return mock;
  }

  // Sets up expectations for the given tracked directories which belong to the
  // same parent directory.
  void ExpectTrackedDirectoryEnumeration(
      const std::vector<FilePath>& child_directories) {
    DCHECK(!child_directories.empty());
    FilePath parent_directory = child_directories[0].DirName();
    // xattr is used to track directories.
    for (const auto& child : child_directories) {
      DCHECK_EQ(parent_directory.value(), child.DirName().value());
      EXPECT_CALL(platform_, GetExtendedFileAttributeAsString(
                                 child, kTrackedDirectoryNameAttribute, _))
          .WillRepeatedly(
              DoAll(SetArgPointee<2>(child.BaseName().value()), Return(true)));
      EXPECT_CALL(platform_, HasExtendedFileAttribute(
                                 child, kTrackedDirectoryNameAttribute))
          .WillRepeatedly(Return(true));
    }
    // |child_directories| should be enumerated as the parent's children.
    auto create_file_enumerator_function = [child_directories]() {
      auto* mock = new NiceMock<MockFileEnumerator>;
      for (const auto& child : child_directories) {
        base::stat_wrapper_t stat = {};
        mock->entries_.push_back(FileEnumerator::FileInfo(child, stat));
      }
      return mock;
    };
    EXPECT_CALL(platform_, GetFileEnumerator(parent_directory, false,
                                             base::FileEnumerator::DIRECTORIES))
        .WillRepeatedly(InvokeWithoutArgs(create_file_enumerator_function));
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

 protected:
  MakeTests test_helper_;
  NiceMock<MockPlatform> platform_;
  Crypto crypto_;
  std::vector<FilePath> homedir_paths_;
  std::vector<FilePath> user_paths_;
  std::vector<std::string> obfuscated_users_;
  MockUserOldestActivityTimestampCache timestamp_cache_;
  std::vector<base::Time> homedir_times_;
  MockVaultKeysetFactory vault_keyset_factory_;
  HomeDirs homedirs_;

  static const uid_t kAndroidSystemRealUid =
      HomeDirs::kAndroidSystemUid + kArcContainerShiftUid;

 private:
  DISALLOW_COPY_AND_ASSIGN(OldHomeDirsTest);
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         OldHomeDirsTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         OldHomeDirsTest,
                         ::testing::Values(false));

TEST_P(OldHomeDirsTest, GoodDecryptTest) {
  // create a HomeDirs instance that points to a good shadow root, test that it
  // properly authenticates against the first key.
  SecureBlob system_salt;
  NiceMock<MockTpm> tpm;
  homedirs_.crypto()->set_tpm(&tpm);
  homedirs_.crypto()->set_use_tpm(false);
  ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt));
  set_policy(false, "", false, "");

  test_helper_.users[1].InjectKeyset(&platform_);
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(test_helper_.users[1].password,
                                        system_salt, &passkey);
  Credentials credentials(test_helper_.users[1].username, passkey);

  ASSERT_TRUE(homedirs_.AreCredentialsValid(credentials));
}

TEST_P(OldHomeDirsTest, BadDecryptTest) {
  // create a HomeDirs instance that points to a good shadow root, test that it
  // properly denies access with a bad passkey
  SecureBlob system_salt;
  NiceMock<MockTpm> tpm;
  homedirs_.crypto()->set_tpm(&tpm);
  homedirs_.crypto()->set_use_tpm(false);
  set_policy(false, "", false, "");

  test_helper_.users[4].InjectKeyset(&platform_);
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey("bogus", system_salt, &passkey);
  Credentials credentials(test_helper_.users[4].username, passkey);

  ASSERT_FALSE(homedirs_.AreCredentialsValid(credentials));
}

#define MAX_VKS 5
class OldKeysetManagementTest : public OldHomeDirsTest {
 public:
  OldKeysetManagementTest() {}
  virtual ~OldKeysetManagementTest() {}

  void SetUp() {
    OldHomeDirsTest::SetUp();
    last_vk_ = -1;
    active_vk_ = NULL;
    memset(active_vks_, 0, sizeof(active_vks_));
    ON_CALL(platform_, CloseFile(_)).WillByDefault(Return(true));
  }

  void TearDown() {
    OldHomeDirsTest::TearDown();
    last_vk_++;
    for (; last_vk_ < MAX_VKS; ++last_vk_) {
      if (active_vks_[last_vk_])
        delete active_vks_[last_vk_];
      active_vks_[last_vk_] = NULL;
    }
    last_vk_ = -1;
    active_vk_ = NULL;
  }

  virtual bool VkDecrypt0(const brillo::SecureBlob& key,
                          bool locked_to_single_user,
                          CryptoError* crypto_error) {
    return memcmp(key.data(), keys_[0].data(), key.size()) == 0;
  }

  virtual const SerializedVaultKeyset& FakeSerialized() const {
    return serialized_;
  }

  virtual SerializedVaultKeyset* FakeMutableSerialized() {
    return &serialized_;
  }

  virtual MockFileEnumerator* NewKeysetFileEnumerator() {
    MockFileEnumerator* files = new MockFileEnumerator();
    {
      InSequence s;
      // Single key.
      EXPECT_CALL(*files, Next()).WillOnce(Return(keyset_paths_[0]));
      EXPECT_CALL(*files, Next()).WillOnce(Return(FilePath()));
    }
    return files;
  }

  virtual MockVaultKeyset* NewActiveVaultKeyset() {
    last_vk_++;
    CHECK(last_vk_ < MAX_VKS);
    active_vk_ = active_vks_[last_vk_];
    EXPECT_CALL(*active_vk_, Decrypt(_, _, _))
        .WillRepeatedly(Invoke(this, &OldKeysetManagementTest::VkDecrypt0));

    EXPECT_CALL(*active_vk_, serialized())
        .WillRepeatedly(Invoke(this, &OldKeysetManagementTest::FakeSerialized));
    EXPECT_CALL(*active_vk_, mutable_serialized())
        .WillRepeatedly(
            Invoke(this, &OldKeysetManagementTest::FakeMutableSerialized));
    return active_vk_;
  }

  virtual void KeysetSetUp() {
    serialized_.Clear();
    NiceMock<MockTpm> tpm;
    homedirs_.crypto()->set_tpm(&tpm);
    homedirs_.crypto()->set_use_tpm(false);
    ASSERT_TRUE(homedirs_.GetSystemSalt(&system_salt_));
    set_policy(false, "", false, "");

    // Setup the base keyset files for users[1]
    keyset_paths_.push_back(test_helper_.users[1].keyset_path);
    keys_.push_back(test_helper_.users[1].passkey);

    EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(platform_,
                GetFileEnumerator(test_helper_.users[1].base_path, false, _))
        .WillRepeatedly(InvokeWithoutArgs(
            this, &OldKeysetManagementTest::NewKeysetFileEnumerator));

    homedirs_.set_vault_keyset_factory(&vault_keyset_factory_);
    // Pre-allocate VKs so that each call can advance
    // but expectations can be set.
    for (int i = 0; i < MAX_VKS; ++i) {
      active_vks_[i] = new MockVaultKeyset();
      // Move this particular expectation setting here instead of
      // NewActiveVaultKeyset, since this allows us to make some modifications
      // to the expectation in the test itself, if necessary.
      // Also change the cardinality to be WillRepeatedly, since this makes it
      // more forgiving even if we don't make an invocation for a VaultKeyset
      // which isn't used in a test.
      EXPECT_CALL(*active_vks_[i], Load(keyset_paths_[0]))
          .WillRepeatedly(Return(true));
      EXPECT_CALL(*active_vks_[i], set_legacy_index(_))
          .Times(testing::AnyNumber());
      EXPECT_CALL(*active_vks_[i], label()).WillRepeatedly(Return("legacy-0"));
    }
    active_vk_ = active_vks_[0];

    EXPECT_CALL(vault_keyset_factory_, New(_, _))
        .WillRepeatedly(InvokeWithoutArgs(
            this, &OldKeysetManagementTest::NewActiveVaultKeyset));
    SecureBlob passkey;
    cryptohome::Crypto::PasswordToPasskey(test_helper_.users[1].password,
                                          system_salt_, &passkey);
    credentials_.reset(
        new Credentials(test_helper_.users[1].username, passkey));

    // Since most of the tests were written without reset_seed in mind,
    // it is tedious to add expectations to every test, for the situation
    // where a wrapped_reset_seed is not present.
    // So, we instead set the wrapped_reset_seed by default,
    // and have a separate test case where it is not set.
    std::string dummy_reset_seed("DEADBEEF");
    serialized_.set_wrapped_reset_seed(dummy_reset_seed);
  }

  void ClearFakeSerializedResetSeed() {
    serialized_.clear_wrapped_reset_seed();
  }

  int last_vk_;
  MockVaultKeyset* active_vk_;
  MockVaultKeyset* active_vks_[MAX_VKS];
  std::vector<FilePath> keyset_paths_;
  std::vector<brillo::SecureBlob> keys_;
  std::unique_ptr<Credentials> credentials_;
  SecureBlob system_salt_;
  SerializedVaultKeyset serialized_;
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         OldKeysetManagementTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         OldKeysetManagementTest,
                         ::testing::Values(false));

TEST_P(OldKeysetManagementTest, AddKeysetSuccess) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the |credentials_| validation.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 1);
}

TEST_P(OldKeysetManagementTest, AddKeysetClobber) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  serialized_.mutable_key_data()->set_label("current label");
  KeyData key_data;
  key_data.set_label("current label");
  FilePath vk_path("/some/path/master.0");
  // Show that 0 is taken.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  // Let it claim 1 until it searches the labels.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vks_[1], set_legacy_index(_));
  EXPECT_CALL(*active_vks_[1], label()).WillOnce(Return(key_data.label()));
  EXPECT_CALL(*active_vks_[1], legacy_index()).WillOnce(Return(0));
  EXPECT_CALL(*active_vks_[1], source_file()).WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Save(vk_path)).WillOnce(Return(true));
  EXPECT_CALL(platform_,
              DeleteFile(Property(&FilePath::value, EndsWith("master.1")), _))
      .Times(1);

  int index = -1;
  EXPECT_EQ(
      CRYPTOHOME_ERROR_NOT_SET,
      homedirs_.AddKeyset(*credentials_, newkey, &key_data, true, &index));
  EXPECT_EQ(index, 0);
}

TEST_P(OldKeysetManagementTest, AddKeysetNoClobber) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  serialized_.mutable_key_data()->set_label("current label");
  KeyData key_data;
  key_data.set_label("current label");
  // The injected keyset in the fixture handles the |credentials_| validation.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vks_[1], label()).WillOnce(Return(key_data.label()));

  EXPECT_EQ(
      CRYPTOHOME_ERROR_KEY_LABEL_EXISTS,
      homedirs_.AddKeyset(*credentials_, newkey, &key_data, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, UpdateKeysetSuccess) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_secret("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // The injected keyset in the fixture handles the |credentials_| validation.
  serialized_.mutable_key_data()->set_label("current label");
  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file()).WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_secret, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path)).WillOnce(Return(true));

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.UpdateKeyset(*credentials_,
                                   const_cast<const Key*>(&new_key), ""));
  EXPECT_EQ(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedNoSignature) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(1);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  // Allow the default override on the revision.
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  EXPECT_EQ(CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
            homedirs_.UpdateKeyset(*credentials_,
                                   const_cast<const Key*>(&new_key), ""));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedSuccess) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_pass("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // Allow updating over an undefined revision.
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file()).WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_pass, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path)).WillOnce(Return(true));

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(
      CRYPTOHOME_ERROR_NOT_SET,
      homedirs_.UpdateKeyset(*credentials_, const_cast<const Key*>(&new_key),
                             hmac.to_string()));
  EXPECT_EQ(serialized_.key_data().revision(), new_key.data().revision());
}

// Ensure signing matches the test vectors in Chrome.
TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedCompatVector) {
  KeysetSetUp();

  // The salted password passed in from Chrome.
  const char kPassword[] = "OSL3HZZSfK+mDQTYUh3lXhgAzJNWhYz52ax0Bleny7Q=";
  // A no-op encryption key.
  const char kB64CipherKey[] = "QUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUFBQUE=\n";
  // The signing key pre-installed.
  const char kB64SigningKey[] =
      "p5TR/34XX0R7IMuffH14BiL1vcdSD8EajPzdIg09z9M=\n";
  // The HMAC-256 signature over kPassword using kSigningKey.
  const char kB64Signature[] = "KOPQmmJcMr9iMkr36N1cX+G9gDdBBu7zutAxNayPMN4=\n";

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_pass(kPassword);
  Key new_key;
  new_key.set_secret(std::string(kPassword, sizeof(kPassword) - 1));
  new_key.mutable_data()->set_label("new label");
  // The compat revision to test is '1'.
  new_key.mutable_data()->set_revision(1);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->set_revision(0);
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  // Add an encryption secret to ensure later upgrades are viable.
  auth_secret->mutable_usage()->set_encrypt(true);
  std::string cipher_key;
  ASSERT_TRUE(brillo::data_encoding::Base64Decode(kB64CipherKey, &cipher_key));
  auth_secret->set_symmetric_key(cipher_key);
  // Add the signing key
  auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  std::string signing_key;
  ASSERT_TRUE(
      brillo::data_encoding::Base64Decode(kB64SigningKey, &signing_key));
  auth_secret->set_symmetric_key(signing_key);

  FilePath vk_path("/some/path/master.0");
  EXPECT_CALL(*active_vk_, source_file()).WillOnce(ReturnRef(vk_path));
  EXPECT_CALL(*active_vk_, Encrypt(new_pass, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, Save(vk_path)).WillOnce(Return(true));

  std::string signature;
  ASSERT_TRUE(brillo::data_encoding::Base64Decode(kB64Signature, &signature));
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.UpdateKeyset(
                *credentials_, const_cast<const Key*>(&new_key), signature));
  EXPECT_EQ(new_key.data().revision(), serialized_.key_data().revision());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedNoEqualReplay) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(100);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_revision(100);
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));
  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(
      CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
      homedirs_.UpdateKeyset(*credentials_, const_cast<const Key*>(&new_key),
                             hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedNoLessReplay) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_revision(1);
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  ASSERT_TRUE(new_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(
      CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
      homedirs_.UpdateKeyset(*credentials_, const_cast<const Key*>(&new_key),
                             hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetAuthorizedBadSignature) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  new_key.mutable_data()->set_revision(0);
  // The injected keyset in the fixture handles the |credentials_| validation.
  KeyData* key_data = serialized_.mutable_key_data();
  key_data->set_label("current label");
  key_data->mutable_privileges()->set_update(false);
  key_data->mutable_privileges()->set_authorized_update(true);
  KeyAuthorizationData* auth_data = key_data->add_authorization_data();
  auth_data->set_type(KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256);
  KeyAuthorizationSecret* auth_secret = auth_data->add_secrets();
  auth_secret->mutable_usage()->set_sign(true);
  const std::string kSomeHMACKey("abc123");
  auth_secret->set_symmetric_key(kSomeHMACKey);

  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret bad_secret;
  bad_secret.set_revision(new_key.data().revision());
  bad_secret.set_secret("something else");
  ASSERT_TRUE(bad_secret.SerializeToString(&changes_str));

  brillo::SecureBlob hmac_key(auth_secret->symmetric_key());
  brillo::SecureBlob hmac_data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, hmac_data);
  EXPECT_EQ(
      CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID,
      homedirs_.UpdateKeyset(*credentials_, const_cast<const Key*>(&new_key),
                             hmac.to_string()));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetBadSecret) {
  KeysetSetUp();

  // No need to do PasswordToPasskey as that is the
  // external callers job.
  SecureBlob new_secret("why not");
  Key new_key;
  new_key.set_secret("why not");
  new_key.mutable_data()->set_label("new label");
  // The injected keyset in the fixture handles the |credentials_| validation.
  serialized_.mutable_key_data()->set_label("current label");

  SecureBlob bad_pass("not it");
  credentials_.reset(new Credentials(test_helper_.users[1].username, bad_pass));
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            homedirs_.UpdateKeyset(*credentials_,
                                   const_cast<const Key*>(&new_key), ""));
  EXPECT_NE(serialized_.key_data().label(), new_key.data().label());
}

TEST_P(OldKeysetManagementTest, UpdateKeysetNotFoundWithLabel) {
  KeysetSetUp();

  KeyData some_label;
  some_label.set_label("key that doesn't exist");
  credentials_->set_key_data(some_label);
  const Key new_key;
  EXPECT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND,
            homedirs_.UpdateKeyset(*credentials_, &new_key, ""));
}

TEST_P(OldKeysetManagementTest, RemoveKeysetSuccess) {
  KeysetSetUp();

  Key remove_key;
  remove_key.mutable_data()->set_label("remove me");

  // Expect the 0 slot since it'll match all the fake keys.
  EXPECT_CALL(*active_vks_[0], set_legacy_index(0));
  EXPECT_CALL(*active_vks_[0], label()).WillRepeatedly(Return("remove me"));
  // Return a different slot to make sure the code is using the right object.
  EXPECT_CALL(*active_vks_[0], legacy_index()).WillOnce(Return(1));
  // The VaultKeyset which will be removed will get index 2.
  EXPECT_CALL(*active_vks_[2],
              Load(keyset_paths_[0].ReplaceExtension(std::to_string(1))))
      .WillOnce(Return(true));

  serialized_.mutable_key_data()->mutable_privileges()->set_remove(true);
  serialized_.mutable_key_data()->set_label("remove me");
  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.RemoveKeyset(*credentials_, remove_key.data()));
}

TEST_P(OldKeysetManagementTest, RemoveKeysetNotFound) {
  KeysetSetUp();

  Key remove_key;
  remove_key.mutable_data()->set_label("remove me please");

  serialized_.mutable_key_data()->mutable_privileges()->set_remove(true);
  serialized_.mutable_key_data()->set_label("the only key in town");
  EXPECT_EQ(CRYPTOHOME_ERROR_KEY_NOT_FOUND,
            homedirs_.RemoveKeyset(*credentials_, remove_key.data()));
}

TEST_P(OldKeysetManagementTest, GetVaultKeysetLabelsOneLabeled) {
  KeysetSetUp();

  serialized_.mutable_key_data()->set_label("a labeled key");
  std::vector<std::string> labels;
  EXPECT_CALL(*active_vks_[0], label()).WillRepeatedly(Return("a labeled key"));
  EXPECT_TRUE(homedirs_.GetVaultKeysetLabels(
      credentials_->GetObfuscatedUsername(system_salt_), &labels));
  ASSERT_NE(0, labels.size());
  EXPECT_EQ(serialized_.key_data().label(), labels[0]);
}

TEST_P(OldKeysetManagementTest, GetVaultKeysetLabelsOneLegacyLabeled) {
  KeysetSetUp();

  serialized_.clear_key_data();
  std::vector<std::string> labels;
  EXPECT_TRUE(homedirs_.GetVaultKeysetLabels(
      credentials_->GetObfuscatedUsername(system_salt_), &labels));
  ASSERT_NE(0, labels.size());
  EXPECT_EQ(StringPrintf("%s%d", kKeyLegacyPrefix, 0), labels[0]);
}

TEST_P(OldKeysetManagementTest, AddKeysetInvalidCreds) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;

  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);
  // Try to authenticate with an unknown key.
  Credentials bad_credentials(test_helper_.users[1].username, newkey);
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED,
            homedirs_.AddKeyset(bad_credentials, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, AddKeysetInvalidPrivileges) {
  // Check for key use that lacks valid add privileges
  KeysetSetUp();

  // The injected keyset in the fixture handles the |credentials_| validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  serialized_.mutable_key_data()->mutable_privileges()->set_add(false);
  int index = -1;
  // Tery to authenticate with a key that cannot add keys.
  ASSERT_EQ(CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, AddKeyset0Available) {
  // While this doesn't affect the hole-finding logic, it's good to cover the
  // full logical behavior by changing which key auths too.
  // master.0 -> master.1
  FilePath new_keyset = test_helper_.users[1].keyset_path.ReplaceExtension("1");
  test_helper_.users[1].keyset_path = new_keyset;
  KeysetSetUp();

  // The injected keyset in the fixture handles the |credentials_| validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  int index = -1;
  // Try to authenticate with an unknown key.
  ASSERT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 0);
}

TEST_P(OldKeysetManagementTest, AddKeyset10Available) {
  KeysetSetUp();

  // The injected keyset in the fixture handles the |credentials_| validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(platform_, OpenFile(Property(&FilePath::value,
                                           MatchesRegex(".*/master\\..$")),
                                  StrEq("wx")))
      .Times(10)
      .WillRepeatedly(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.10")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith("master.10"))))
      .WillOnce(Return(true));

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 10);
}

TEST_P(OldKeysetManagementTest, AddKeysetNoFreeIndices) {
  KeysetSetUp();

  // The injected keyset in the fixture handles the |credentials_| validation.
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);

  EXPECT_CALL(platform_, OpenFile(Property(&FilePath::value,
                                           MatchesRegex(".*/master\\..*$")),
                                  StrEq("wx")))
      .Times(kKeyFileMax)
      .WillRepeatedly(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  int index = -1;
  ASSERT_EQ(CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, AddKeysetEncryptFail) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the |credentials_| validation.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      DeleteFile(Property(&FilePath::value, EndsWith("master.0")), false))
      .WillOnce(Return(true));
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, AddKeysetSaveFail) {
  KeysetSetUp();

  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;
  // The injected keyset in the fixture handles the |credentials_| validation.
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.0")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      DeleteFile(Property(&FilePath::value, EndsWith("master.0")), false))
      .WillOnce(Return(true));
  ASSERT_EQ(CRYPTOHOME_ERROR_BACKING_STORE_FAILURE,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, -1);
}

TEST_P(OldKeysetManagementTest, AddKeysetNoResetSeedSuccess) {
  KeysetSetUp();
  ClearFakeSerializedResetSeed();

  std::string old_file_name("master.0");

  const SecureBlob oldkey = credentials_->passkey();
  SecureBlob newkey;
  cryptohome::Crypto::PasswordToPasskey("why not", system_salt_, &newkey);
  int index = -1;

  // Expectations for calls used to generate the reset_seed
  base::FilePath orig_file(old_file_name);
  EXPECT_CALL(*active_vk_, Encrypt(oldkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith(old_file_name))))
      .WillOnce(Return(true));
  EXPECT_CALL(*active_vk_, source_file()).WillOnce(ReturnRef(orig_file));

  // The injected keyset in the fixture handles the |credentials_| validation.
  EXPECT_CALL(platform_,
              OpenFile(Property(&FilePath::value, EndsWith(old_file_name)),
                       StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(*active_vk_, Encrypt(newkey, _)).WillOnce(Return(true));
  EXPECT_CALL(*active_vk_,
              Save(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _)).Times(0);

  EXPECT_EQ(CRYPTOHOME_ERROR_NOT_SET,
            homedirs_.AddKeyset(*credentials_, newkey, NULL, false, &index));
  EXPECT_EQ(index, 1);
}

TEST_P(OldKeysetManagementTest, ForceRemoveKeysetSuccess) {
  KeysetSetUp();
  EXPECT_CALL(
      platform_,
      DeleteFile(Property(&FilePath::value, EndsWith("master.0")), false))
      .WillOnce(Return(true));
  // There is only one call to VaultKeyset, so it gets the MockVaultKeyset
  // with index 0.
  EXPECT_CALL(*active_vks_[0], Load(_)).WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(OldKeysetManagementTest, ForceRemoveKeysetMissingKeyset) {
  KeysetSetUp();
  // There is only one call to VaultKeyset, so it gets the MockVaultKeyset
  // with index 0.
  // Set it to false, since there is no valid VaultKeyset.
  EXPECT_CALL(*active_vks_[0], Load(_)).WillOnce(Return(false));
  ASSERT_TRUE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(OldKeysetManagementTest, ForceRemoveKeysetNegativeIndex) {
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", -1));
}

TEST_P(OldKeysetManagementTest, ForceRemoveKeysetOverMaxIndex) {
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", kKeyFileMax));
}

TEST_P(OldKeysetManagementTest, ForceRemoveKeysetFailedDelete) {
  KeysetSetUp();
  EXPECT_CALL(
      platform_,
      DeleteFile(Property(&FilePath::value, EndsWith("master.0")), false))
      .WillOnce(Return(false));
  // There is only one call to VaultKeyset, so it gets the MockVaultKeyset
  // with index 0.
  EXPECT_CALL(*active_vks_[0], Load(_)).WillOnce(Return(true));
  ASSERT_FALSE(homedirs_.ForceRemoveKeyset("a0b0c0", 0));
}

TEST_P(OldKeysetManagementTest, MoveKeysetSuccess_0_to_1) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
              Rename(Property(&FilePath::value, EndsWith("master.0")),
                     Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
      .WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetSuccess_1_to_99) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.99"))))
      .WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.99")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
              Rename(Property(&FilePath::value, EndsWith("master.1")),
                     Property(&FilePath::value, EndsWith("master.99"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
      .WillOnce(Return(true));
  ASSERT_TRUE(homedirs_.MoveKeyset(obfuscated, 1, 99));
}

TEST_P(OldKeysetManagementTest, MoveKeysetNegativeSource) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, -1, 1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetNegativeDestination) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 1, -1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetTooLargeDestination) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 1, kKeyFileMax));
}

TEST_P(OldKeysetManagementTest, MoveKeysetTooLargeSource) {
  const std::string obfuscated = "a0b0c0";
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, kKeyFileMax, 0));
}

TEST_P(OldKeysetManagementTest, MoveKeysetMissingSource) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(false));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetDestinationExists) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(true));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetExclusiveOpenFailed) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(NULL)));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

TEST_P(OldKeysetManagementTest, MoveKeysetRenameFailed) {
  const std::string obfuscated = "a0b0c0";
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.0"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FileExists(Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      OpenFile(Property(&FilePath::value, EndsWith("master.1")), StrEq("wx")))
      .WillOnce(Return(reinterpret_cast<FILE*>(0xbeefbeef)));
  EXPECT_CALL(platform_,
              Rename(Property(&FilePath::value, EndsWith("master.0")),
                     Property(&FilePath::value, EndsWith("master.1"))))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, CloseFile(reinterpret_cast<FILE*>(0xbeefbeef)))
      .WillOnce(Return(true));
  ASSERT_FALSE(homedirs_.MoveKeyset(obfuscated, 0, 1));
}

}  // namespace cryptohome
