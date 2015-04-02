// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mount.

#include "cryptohome/mount.h"

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>  // For memset(), memcpy()
#include <sys/types.h>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <base/strings/stringprintf.h>
#include <chromeos/cryptohome.h>
#include <chromeos/secure_blob.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_boot_lockbox.h"
#include "cryptohome/mock_chaps_client_factory.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/mock_user_session.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/username_passkey.h"
#include "cryptohome/vault_keyset.h"

#include "vault_keyset.pb.h"  // NOLINT(build/include)

using base::StringPrintf;
using chromeos::SecureBlob;
using std::string;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::AnyOf;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgumentPointee;
using ::testing::StartsWith;
using ::testing::StrictMock;
using ::testing::Unused;
using ::testing::_;

namespace cryptohome {

const char kImageDir[] = "test_image_dir";
const char kImageSaltFile[] = "test_image_dir/salt";
const char kSkelDir[] = "test_image_dir/skel";
const gid_t kDaemonGid = 400;  // TODO(wad): expose this in mount.h

ACTION_P2(SetOwner, owner_known, owner) {
  if (owner_known)
    *arg0 = owner;
  return owner_known;
}

ACTION_P(SetEphemeralUsersEnabled, ephemeral_users_enabled) {
  *arg0 = ephemeral_users_enabled;
  return true;
}

// Straight pass through.
Tpm::TpmRetryAction TpmPassthroughEncrypt(uint32_t _key,
                                          const SecureBlob &plaintext,
                                          Unused,
                                          SecureBlob *ciphertext) {
  ciphertext->resize(plaintext.size());
  memcpy(ciphertext->data(), plaintext.data(), plaintext.size());
  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction TpmPassthroughDecrypt(uint32_t _key,
                                          const SecureBlob &ciphertext,
                                          Unused,
                                          SecureBlob *plaintext) {
  plaintext->resize(ciphertext.size());
  memcpy(plaintext->data(), ciphertext.data(), ciphertext.size());
  return Tpm::kTpmRetryNone;
}

class MountTest : public ::testing::Test {
 public:
  MountTest() { }
  virtual ~MountTest() { }

  void SetUp() {
    // Populate the system salt
    helper_.SetUpSystemSalt();
    helper_.InjectSystemSalt(&platform_, kImageSaltFile);

    // Setup default uid/gid values
    chronos_uid_ = 1000;
    chronos_gid_ = 1000;
    shared_gid_ = 1001;
    chaps_uid_ = 223;

    crypto_.set_tpm(&tpm_);
    crypto_.set_platform(&platform_);
    crypto_.set_use_tpm(false);

    user_timestamp_cache_.reset(new UserOldestActivityTimestampCache());
    mount_ = new Mount();
    mount_->set_homedirs(&homedirs_);
    mount_->set_use_tpm(false);
    mount_->set_shadow_root(kImageDir);
    mount_->set_skel_source(kSkelDir);
    mount_->set_chaps_client_factory(&chaps_client_factory_);
    homedirs_.set_crypto(&crypto_);
    homedirs_.set_platform(&platform_);
    homedirs_.set_shadow_root(kImageDir);
    set_policy(false, "", false);
  }

  void TearDown() {
    mount_ = NULL;
    helper_.TearDownSystemSalt();
  }

  void InsertTestUsers(const TestUserInfo* user_info_list, int count) {
    helper_.InitTestData(kImageDir, user_info_list,
                         static_cast<size_t>(count));
  }

  bool DoMountInit() {
    EXPECT_CALL(platform_, GetUserId("chronos", _, _))
      .WillOnce(DoAll(SetArgumentPointee<1>(chronos_uid_),
                      SetArgumentPointee<2>(chronos_gid_),
                      Return(true)));
    EXPECT_CALL(platform_, GetUserId("chaps", _, _))
      .WillOnce(DoAll(SetArgumentPointee<1>(chaps_uid_),
                      SetArgumentPointee<2>(shared_gid_),
                      Return(true)));
    EXPECT_CALL(platform_, GetGroupId("chronos-access", _))
      .WillOnce(DoAll(SetArgumentPointee<1>(shared_gid_),
                      Return(true)));
    return mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get());
  }

  bool LoadSerializedKeyset(const chromeos::Blob& contents,
                            cryptohome::SerializedVaultKeyset* serialized) {
    CHECK_NE(contents.size(), 0U);
    return serialized->ParseFromArray(contents.data(), contents.size());
  }

  void GetKeysetBlob(const SerializedVaultKeyset& serialized,
                     SecureBlob* blob) {
    SecureBlob local_wrapped_keyset(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(
        local_wrapped_keyset.char_data(),
        serialized.wrapped_keyset().length(), 0);
    blob->swap(local_wrapped_keyset);
  }

  void set_policy(bool owner_known,
                  const string& owner,
                  bool ephemeral_users_enabled) {
    policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy, LoadPolicy())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*device_policy, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*device_policy, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
    mount_->set_policy_provider(new policy::PolicyProvider(device_policy));
    // By setting a policy up, we're expecting HomeDirs::GetPlainOwner() to
    // actually execute the code rather than a mock.
    EXPECT_CALL(homedirs_, GetPlainOwner(_))
        .WillRepeatedly(Invoke(&homedirs_, &MockHomeDirs::ActualGetPlainOwner));
  }

 protected:
  // Protected for trivial access.
  MakeTests helper_;
  uid_t chronos_uid_;
  gid_t chronos_gid_;
  uid_t chaps_uid_;
  gid_t shared_gid_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockTpmInit> tpm_init_;
  NiceMock<MockHomeDirs> homedirs_;
  NiceMock<MockCrypto> crypto_;
  MockChapsClientFactory chaps_client_factory_;
  scoped_refptr<Mount> mount_;
  scoped_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTest);
};

TEST_F(MountTest, BadInitTest) {
  // Create a Mount instance that points to a bad shadow root.
  mount_->set_shadow_root("/dev/null");

  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(kDefaultUsers[0].password,
                                        helper_.system_salt, &passkey);
  UsernamePasskey up(kDefaultUsers[0].username, passkey);

  // Shadow root creation should fail.
  EXPECT_CALL(platform_, DirectoryExists("/dev/null"))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, CreateDirectory("/dev/null"))
    .WillOnce(Return(false));
  // Salt creation failure because shadow_root is bogus.
  EXPECT_CALL(platform_, FileExists("/dev/null/salt"))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, WriteFileAtomicDurable("/dev/null/salt", _, _))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetUserId("chronos", _, _))
    .WillOnce(DoAll(SetArgumentPointee<1>(1000), SetArgumentPointee<2>(1000),
                    Return(true)));
  EXPECT_CALL(platform_, GetUserId("chaps", _, _))
    .WillOnce(DoAll(SetArgumentPointee<1>(1001), SetArgumentPointee<2>(1001),
                    Return(true)));
  EXPECT_CALL(platform_, GetGroupId("chronos-access", _))
    .WillOnce(DoAll(SetArgumentPointee<1>(1002), Return(true)));
  EXPECT_FALSE(mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get()));
  ASSERT_FALSE(mount_->AreValid(up));
}

TEST_F(MountTest, CurrentCredentialsTest) {
  // Create a Mount instance that points to a good shadow root, test that it
  // properly authenticates against the first key.
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(kDefaultUsers[3].password,
                                        helper_.system_salt, &passkey);
  UsernamePasskey up(kDefaultUsers[3].username, passkey);

  EXPECT_TRUE(DoMountInit());

  NiceMock<MockUserSession> user_session;
  user_session.Init(SecureBlob());
  user_session.SetUser(up);
  mount_->set_current_user(&user_session);

  EXPECT_CALL(user_session, CheckUser(_))
      .WillOnce(Return(true));
  EXPECT_CALL(user_session, Verify(_))
      .WillOnce(Return(true));

  ASSERT_TRUE(mount_->AreValid(up));
}

TEST_F(MountTest, BadDecryptTest) {
  // Create a Mount instance that points to a good shadow root, test that it
  // properly denies access with a bad passkey.
  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey("bogus", helper_.system_salt, &passkey);
  UsernamePasskey up(kDefaultUsers[4].username, passkey);

  EXPECT_TRUE(DoMountInit());
  ASSERT_FALSE(mount_->AreValid(up));
}

TEST_F(MountTest, MountCryptohomeNoPrivileges) {
  // Check that Mount only works if the mount permission is given.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, SetMask(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser *user = &helper_.users[0];
  user->key_data.set_label("my key!");
  user->use_key_data = true;
  user->key_data.mutable_privileges()->set_mount(false);
  // Regenerate the serialized vault keyset.
  user->GenerateCredentials();
  UsernamePasskey up(user->username, user->passkey);
  // Let the legacy key iteration work here.

  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_, shared_gid_,
                        kDaemonGid);
  user->InjectKeyset(&platform_, false);

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));

  MountError error = MOUNT_ERROR_NONE;
  EXPECT_FALSE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));
  EXPECT_EQ(MOUNT_ERROR_KEY_FAILURE, error);
}

TEST_F(MountTest, MountCryptohomeHasPrivileges) {
  // Check that Mount only works if the mount permission is given.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, SetMask(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser *user = &helper_.users[0];
  user->key_data.set_label("my key!");
  user->use_key_data = true;
  user->key_data.mutable_privileges()->set_mount(true);
  // Regenerate the serialized vault keyset.
  user->GenerateCredentials();
  UsernamePasskey up(user->username, user->passkey);
  // Let the legacy key iteration work here.

  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_, shared_gid_,
                        kDaemonGid);
  user->InjectKeyset(&platform_, false);

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
    .Times(2)
    .WillRepeatedly(Return(0));
  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
    .WillOnce(Return(false));
  // user exists, so there'll be no skel copy after.
  EXPECT_CALL(platform_, Mount(_, _, _, _))
    .WillRepeatedly(Return(true));
  // Only one mount, so the legacy mount point is used.
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, Bind(_, _))
    .WillRepeatedly(Return(true));

  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));

  EXPECT_CALL(platform_, Unmount(_, _, _))
      .Times(5)
      .WillRepeatedly(Return(true));

  // Unmount here to avoid the scoped Mount doing it implicitly.
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::Now()));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));
  EXPECT_TRUE(mount_->UnmountCryptohome());
}


// A fixture for testing chaps directory checks.
class ChapsDirectoryTest : public ::testing::Test {
 public:
  ChapsDirectoryTest()
      : kBaseDir("/base_chaps_dir"),
        kSaltFile("/base_chaps_dir/auth_data_salt"),
        kDatabaseDir("/base_chaps_dir/database"),
        kDatabaseFile("/base_chaps_dir/database/file"),
        kLegacyDir("/legacy"),
        kRootUID(0), kRootGID(0), kChapsUID(1), kSharedGID(2),
        mount_(new Mount()),
        user_timestamp_cache_(new UserOldestActivityTimestampCache()) {
    crypto_.set_platform(&platform_);
    mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get());
    mount_->chaps_user_ = kChapsUID;
    mount_->default_access_group_ = kSharedGID;
    // By default, set stats to the expected values.
    InitStat(&base_stat_, 040750, kChapsUID, kSharedGID);
    InitStat(&salt_stat_, 0600, kRootUID, kRootGID);
    InitStat(&database_dir_stat_, 040750, kChapsUID, kSharedGID);
    InitStat(&database_file_stat_, 0640, kChapsUID, kSharedGID);
  }

  virtual ~ChapsDirectoryTest() {}

  void SetupFakeChapsDirectory() {
    // Configure the base directory.
    EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, Stat(kBaseDir, _))
        .WillRepeatedly(DoAll(SetArgumentPointee<1>(base_stat_), Return(true)));

    // Configure a fake enumerator.
    MockFileEnumerator* enumerator = platform_.mock_enumerator();
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kBaseDir, base_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kSaltFile, salt_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kDatabaseDir, database_dir_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kDatabaseFile, database_file_stat_));
  }

  bool RunCheck() {
    return mount_->CheckChapsDirectory(kBaseDir, kLegacyDir);
  }

 protected:
  const std::string kBaseDir;
  const std::string kSaltFile;
  const std::string kDatabaseDir;
  const std::string kDatabaseFile;
  const std::string kLegacyDir;
  const uid_t kRootUID;
  const gid_t kRootGID;
  const uid_t kChapsUID;
  const gid_t kSharedGID;

  struct stat base_stat_;
  struct stat salt_stat_;
  struct stat database_dir_stat_;
  struct stat database_file_stat_;

  scoped_refptr<Mount> mount_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCrypto> crypto_;
  scoped_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;

 private:
  void InitStat(struct stat* s, mode_t mode, uid_t uid, gid_t gid) {
    memset(s, 0, sizeof(struct stat));
    s->st_mode = mode;
    s->st_uid = uid;
    s->st_gid = gid;
  }

  DISALLOW_COPY_AND_ASSIGN(ChapsDirectoryTest);
};

TEST_F(ChapsDirectoryTest, DirectoryOK) {
  SetupFakeChapsDirectory();
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, DirectoryDoesNotExist) {
  // Specify directory does not exist.
  EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(kLegacyDir))
      .WillRepeatedly(Return(false));
  // Expect basic setup.
  EXPECT_CALL(platform_, CreateDirectory(kBaseDir))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kBaseDir, 0750))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kBaseDir, kChapsUID, kSharedGID))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, CreateFailure) {
  // Specify directory does not exist.
  EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(kLegacyDir))
      .WillRepeatedly(Return(false));
  // Expect basic setup but fail.
  EXPECT_CALL(platform_, CreateDirectory(kBaseDir))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadPerms) {
  // Specify some bad perms.
  base_stat_.st_mode = 040700;
  salt_stat_.st_mode = 0640;
  database_dir_stat_.st_mode = 040755;
  database_file_stat_.st_mode = 0666;
  SetupFakeChapsDirectory();
  // Expect corrections.
  EXPECT_CALL(platform_, SetPermissions(kBaseDir, 0750))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kSaltFile, 0600))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kDatabaseDir, 0750))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kDatabaseFile, 0640))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadOwnership) {
  // Specify bad ownership.
  base_stat_.st_uid = kRootUID;
  salt_stat_.st_gid = kChapsUID;
  database_dir_stat_.st_gid = kChapsUID;
  database_file_stat_.st_uid = kSharedGID;
  SetupFakeChapsDirectory();
  // Expect corrections.
  EXPECT_CALL(platform_, SetOwnership(kBaseDir, kChapsUID, kSharedGID))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kSaltFile, kRootUID, kRootGID))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kDatabaseDir, kChapsUID, kSharedGID))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kDatabaseFile, kChapsUID, kSharedGID))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadPermsFailure) {
  // Specify some bad perms.
  base_stat_.st_mode = 040700;
  SetupFakeChapsDirectory();
  // Expect corrections but fail to apply.
  EXPECT_CALL(platform_, SetPermissions(_, _))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadOwnershipFailure) {
  // Specify bad ownership.
  base_stat_.st_uid = kRootUID;
  SetupFakeChapsDirectory();
  // Expect corrections but fail to apply.
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

TEST_F(MountTest, CheckChapsDirectoryMigration) {
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));

  // Configure stub methods.
  EXPECT_CALL(platform_, Copy(_, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
      .WillRepeatedly(Return(true));

  // Stubs which will trigger the migration code path.
  EXPECT_CALL(platform_, DirectoryExists("/fake"))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists("/fake_legacy"))
      .WillRepeatedly(Return(true));

  // Configure stat for the base directory.
  struct stat base_stat = {0};
  base_stat.st_mode = 040123;
  base_stat.st_uid = 1;
  base_stat.st_gid = 2;
  EXPECT_CALL(platform_, Stat(_, _))
      .WillRepeatedly(DoAll(SetArgumentPointee<1>(base_stat), Return(true)));

  // Configure a fake enumerator.
  MockFileEnumerator* enumerator = platform_.mock_enumerator();
  struct stat file_info1 = {0};
  file_info1.st_mode = 0555;
  file_info1.st_uid = 3;
  file_info1.st_gid = 4;
  struct stat file_info2 = {0};
  file_info2.st_mode = 0777;
  file_info2.st_uid = 5;
  file_info2.st_gid = 6;
  enumerator->entries_.push_back(
      FileEnumerator::FileInfo("/fake_legacy/test_file1", file_info1));
  enumerator->entries_.push_back(
      FileEnumerator::FileInfo("test_file2", file_info2));

  // These expectations will ensure the ownership and permissions are being
  // correctly applied after the directory has been moved.
  EXPECT_CALL(platform_, SetOwnership("/fake/test_file1", 3, 4)).Times(1);
  EXPECT_CALL(platform_, SetPermissions("/fake/test_file1", 0555)).Times(1);
  EXPECT_CALL(platform_, SetOwnership("/fake/test_file2", 5, 6)).Times(1);
  EXPECT_CALL(platform_, SetPermissions("/fake/test_file2", 0777)).Times(1);
  EXPECT_CALL(platform_, SetOwnership("/fake", 1, 2)).Times(1);
  EXPECT_CALL(platform_, SetPermissions("/fake", 0123)).Times(1);

  DoMountInit();
  EXPECT_TRUE(mount_->CheckChapsDirectory("/fake", "/fake_legacy"));
}

TEST_F(MountTest, CreateCryptohomeTest) {
  InsertTestUsers(&kDefaultUsers[5], 1);
  // Creates a cryptohome and tests credentials.
  HomeDirs homedirs;
  homedirs.set_shadow_root(kImageDir);

  TestUser *user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  EXPECT_TRUE(DoMountInit());
  EXPECT_TRUE(homedirs.Init(&platform_, mount_->crypto(),
                            user_timestamp_cache_.get()));

  // TODO(wad) Make this into a UserDoesntExist() helper.
  EXPECT_CALL(platform_, FileExists(user->image_path))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_,
      CreateDirectory(
        AnyOf(user->mount_prefix,
              user->user_mount_prefix,
              user->user_mount_path,
              user->root_mount_prefix,
              user->root_mount_path)))
    .Times(7)
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
      CreateDirectory(
        AnyOf("/home/chronos",
              mount_->GetNewUserPath(user->username))))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      CreateDirectory(AnyOf(user->vault_path, user->base_path)))
    .Times(2)
    .WillRepeatedly(Return(true));

  chromeos::Blob creds;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillOnce(DoAll(SaveArg<1>(&creds), Return(true)));

  bool created;
  ASSERT_TRUE(mount_->EnsureCryptohome(up, &created));
  ASSERT_TRUE(created);
  ASSERT_NE(creds.size(), 0);
  ASSERT_FALSE(mount_->AreValid(up));
  {
    InSequence s;
    MockFileEnumerator* files = new MockFileEnumerator();
    EXPECT_CALL(platform_, GetFileEnumerator(user->base_path, false, _))
      .WillOnce(Return(files));
     // Single key.
    EXPECT_CALL(*files, Next())
      .WillOnce(Return(user->keyset_path));
    EXPECT_CALL(*files, Next())
      .WillRepeatedly(Return(""));
  }

  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillOnce(DoAll(SetArgumentPointee<1>(creds), Return(true)));

  ASSERT_TRUE(homedirs.AreCredentialsValid(up));
}

TEST_F(MountTest, GoodReDecryptTest) {
  InsertTestUsers(&kDefaultUsers[6], 1);
  // Create a Mount instance that points to a good shadow root, test that it
  // properly re-authenticates against the first key.
  mount_->set_use_tpm(true);
  crypto_.set_use_tpm(true);

  HomeDirs homedirs;
  homedirs.set_shadow_root(kImageDir);

  TestUser *user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  EXPECT_CALL(tpm_init_, HasCryptohomeKey())
    .WillOnce(Return(false))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_init_, SetupTpm(true))
    .WillOnce(Return(true))  // This is by crypto.Init() and
    .WillOnce(Return(true));  // cause we forced HasCryptohomeKey to false once.
  crypto_.Init(&tpm_init_);

  EXPECT_CALL(tpm_, IsEnabled())
    .WillRepeatedly(Return(true));
  EXPECT_CALL(tpm_, IsOwned())
    .WillRepeatedly(Return(true));

  EXPECT_TRUE(DoMountInit());
  EXPECT_TRUE(homedirs.Init(&platform_, mount_->crypto(),
                            user_timestamp_cache_.get()));

  // Load the pre-generated keyset
  std::string key_path = mount_->GetUserLegacyKeyFileForUser(
      up.GetObfuscatedUsername(helper_.system_salt), 0);
  EXPECT_GT(key_path.size(), 0u);
  cryptohome::SerializedVaultKeyset serialized;
  EXPECT_TRUE(serialized.ParseFromArray(user->credentials.data(),
                                        user->credentials.size()));
  // Ensure we're starting from scrypt so we can test migrate to a mock-TPM.
  EXPECT_EQ((serialized.flags() & SerializedVaultKeyset::SCRYPT_WRAPPED),
            SerializedVaultKeyset::SCRYPT_WRAPPED);

  // Call DecryptVaultKeyset first, allowing migration (the test data is not
  // scrypt nor TPM wrapped) to a TPM-wrapped keyset
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, mount_->crypto());
  MountError error = MOUNT_ERROR_NONE;
  // Inject the pre-generated, scrypt-wrapped keyset.
  EXPECT_CALL(platform_, FileExists(user->keyset_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->credentials),
                          Return(true)));
  EXPECT_CALL(platform_, FileExists(user->salt_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->salt_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->user_salt),
                          Return(true)));

  // Allow the "backup" to be written
  EXPECT_CALL(platform_,
      FileExists(StringPrintf("%s.bak", user->keyset_path.c_str())))
    .Times(2)  // Second time is for Mount::DeleteCacheFiles()
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      FileExists(StringPrintf("%s.bak", user->salt_path.c_str())))
    .Times(2)  // Second time is for Mount::DeleteCacheFiles()
    .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_,
      Move(user->keyset_path,
           StringPrintf("%s.bak", user->keyset_path.c_str())))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_,
      Move(user->salt_path, StringPrintf("%s.bak", user->salt_path.c_str())))
    .WillOnce(Return(true));

  // Create the "TPM-wrapped" value by letting it save the plaintext.
  EXPECT_CALL(tpm_, EncryptBlob(_, _, _, _))
    .WillRepeatedly(Invoke(TpmPassthroughEncrypt));
  chromeos::SecureBlob fake_pub_key("A");
  EXPECT_CALL(tpm_, GetPublicKeyHash(_, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(fake_pub_key),
                          Return(Tpm::kTpmRetryNone)));

  chromeos::Blob migrated_keyset;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillOnce(DoAll(SaveArg<1>(&migrated_keyset), Return(true)));
  int key_index = 0;

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  EXPECT_TRUE(mount_->DecryptVaultKeyset(up, true, &vault_keyset, &serialized,
                                        &key_index, &error));
  ASSERT_EQ(error, MOUNT_ERROR_NONE);
  ASSERT_NE(migrated_keyset.size(), 0);

  cryptohome::SerializedVaultKeyset serialized_tpm;
  EXPECT_TRUE(serialized_tpm.ParseFromArray(migrated_keyset.data(),
                                            migrated_keyset.size()));
  // Did it migrate?
  EXPECT_EQ((serialized_tpm.flags() & SerializedVaultKeyset::TPM_WRAPPED),
            SerializedVaultKeyset::TPM_WRAPPED);

  // Inject the migrated keyset
  Mock::VerifyAndClearExpectations(&platform_);
  EXPECT_CALL(platform_, FileExists(user->keyset_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(migrated_keyset),
                          Return(true)));
  EXPECT_CALL(platform_, FileExists(user->salt_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->salt_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->user_salt),
                          Return(true)));
  EXPECT_CALL(tpm_, DecryptBlob(_, _, _, _))
    .WillRepeatedly(Invoke(TpmPassthroughDecrypt));

    MockFileEnumerator* files = new MockFileEnumerator();
    EXPECT_CALL(platform_, GetFileEnumerator(user->base_path, false, _))
      .WillOnce(Return(files));
     // Single key.
  {
    InSequence s;
    EXPECT_CALL(*files, Next())
      .WillOnce(Return(user->keyset_path));
    EXPECT_CALL(*files, Next())
      .WillOnce(Return(""));
  }

  ASSERT_TRUE(homedirs.AreCredentialsValid(up));
}

TEST_F(MountTest, MountCryptohome) {
  // checks that cryptohome tries to mount successfully, and tests that the
  // tracked directories are created/replaced as expected
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser *user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_, shared_gid_,
                        kDaemonGid);
  user->InjectKeyset(&platform_, false);

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
    .Times(2)
    .WillRepeatedly(Return(0));
  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillRepeatedly(Return(0));

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
    .WillOnce(Return(false));
  // user exists, so there'll be no skel copy after.
  EXPECT_CALL(platform_, Mount(_, _, _, _))
    .WillRepeatedly(Return(true));
  // Only one mount, so the legacy mount point is used.
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, Bind(_, _))
    .WillRepeatedly(Return(true));

  MountError error = MOUNT_ERROR_NONE;
  EXPECT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));
}

TEST_F(MountTest, MountCryptohomeChapsKey) {
  // Test to check if Cryptohome mount saves the chaps key correctly,
  // and doesn't regenerate it.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  InsertTestUsers(&kDefaultUsers[0], 1);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  user->InjectKeyset(&platform_, false);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, mount_->crypto());
  SerializedVaultKeyset serialized;
  MountError error;
  int key_index = -1;
  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  // First we decrypt the vault to load the chaps key.
  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, false, &vault_keyset, &serialized,
                                        &key_index, &error));
  EXPECT_EQ(key_index, key_indices[0]);
  EXPECT_EQ(serialized.has_wrapped_chaps_key(), true);

  SecureBlob local_chaps(vault_keyset.chaps_key().begin(),
                         vault_keyset.chaps_key().end());
  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  ASSERT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));

  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, false, &vault_keyset, &serialized,
                                        &key_index, &error));

  // Compare the pre mount chaps key to the post mount key.
  ASSERT_EQ(local_chaps.size(), vault_keyset.chaps_key().size());
  ASSERT_EQ(0, chromeos::SecureMemcmp(local_chaps.data(),
    vault_keyset.chaps_key().data(), local_chaps.size()));
}

TEST_F(MountTest, MountCryptohomeNoChapsKey) {
  // This test checks if the mount operation recreates the chaps key
  // if it isn't present in the vault.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  InsertTestUsers(&kDefaultUsers[0], 1);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  user->InjectKeyset(&platform_, false);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, mount_->crypto());
  SerializedVaultKeyset serialized;
  MountError error;
  int key_index = -1;
  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillOnce(DoAll(SetArgumentPointee<1>(user->credentials),
                         Return(true)));

  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, false, &vault_keyset, &serialized,
                                        &key_index, &error));

  vault_keyset.clear_chaps_key();
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Move(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillRepeatedly(DoAll(SaveArg<1>(&(user->credentials)), Return(true)));
  ASSERT_TRUE(mount_->ReEncryptVaultKeyset(up, vault_keyset, key_index,
                                           &serialized));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->credentials),
                          Return(true)));
  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, false, &vault_keyset, &serialized,
                                        &key_index, &error));

  EXPECT_EQ(key_index, key_indices[0]);
  EXPECT_EQ(serialized.has_wrapped_chaps_key(), false);

  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  ASSERT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->credentials),
                          Return(true)));
  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, false, &vault_keyset, &serialized,
                                        &key_index, &error));
  EXPECT_EQ(serialized.has_wrapped_chaps_key(), true);
  EXPECT_EQ(vault_keyset.chaps_key().size(), CRYPTOHOME_CHAPS_KEY_LENGTH);
}

TEST_F(MountTest, MountCryptohomeNoChange) {
  // Checks that cryptohome doesn't by default re-save the cryptohome on mount.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  InsertTestUsers(&kDefaultUsers[11], 1);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  user->InjectKeyset(&platform_, false);
  VaultKeyset vault_keyset;
  vault_keyset.Initialize(&platform_, mount_->crypto());
  SerializedVaultKeyset serialized;
  MountError error;
  int key_index = -1;
  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, true, &vault_keyset, &serialized,
                                        &key_index, &error));
  EXPECT_EQ(key_index, key_indices[0]);

  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);

  EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              CreateDirectory(mount_->GetNewUserPath(user->username)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _))
      .Times(4)
      .WillRepeatedly(Return(true));

  ASSERT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));

  SerializedVaultKeyset new_serialized;
  ASSERT_TRUE(mount_->DecryptVaultKeyset(up, true, &vault_keyset,
                                        &new_serialized, &key_index, &error));

  SecureBlob lhs;
  GetKeysetBlob(serialized, &lhs);
  SecureBlob rhs;
  GetKeysetBlob(new_serialized, &rhs);
  ASSERT_EQ(lhs.size(), rhs.size());
  ASSERT_EQ(0, chromeos::SecureMemcmp(lhs.data(), rhs.data(), lhs.size()));
}

TEST_F(MountTest, MountCryptohomeNoCreate) {
  // Checks that doesn't create the cryptohome for the user on Mount without
  // being told to do so.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  // Test user at index 12 hasn't been created
  InsertTestUsers(&kDefaultUsers[12], 1);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  user->InjectKeyset(&platform_, false);

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  // Doesn't exist.
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
    .WillOnce(Return(false));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = false;
  MountError error = MOUNT_ERROR_NONE;
  ASSERT_FALSE(mount_->MountCryptohome(up, mount_args, &error));
  ASSERT_EQ(MOUNT_ERROR_USER_DOES_NOT_EXIST, error);

  // Now let it create the vault.
  // TODO(wad) Drop NiceMock and replace with InSequence EXPECT_CALL()s.
  // It will complain about creating tracked subdirs, but that is non-fatal.
  Mock::VerifyAndClearExpectations(&platform_);
  user->InjectKeyset(&platform_, false);

  EXPECT_CALL(platform_,
      DirectoryExists(AnyOf(user->vault_path, user->user_vault_path)))
    .Times(2)
    .WillRepeatedly(Return(false));

  // Not legacy
  EXPECT_CALL(platform_, FileExists(user->image_path))
    .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  chromeos::Blob creds;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillOnce(DoAll(SaveArg<1>(&creds), Return(true)))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
    .WillOnce(Return(false));  // mount precondition
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // bind precondition for first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .Times(4)
      .WillRepeatedly(Return(true));

  // Fake successful mount to /home/chronos/user/*
  EXPECT_CALL(platform_, FileExists(AnyOf(
                           StartsWith(user->legacy_user_mount_path),
                           StartsWith(user->vault_mount_path))))
    .WillRepeatedly(Return(true));

  mount_args.create_if_missing = true;
  error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));
  ASSERT_EQ(MOUNT_ERROR_NONE, error);
}

TEST_F(MountTest, UserActivityTimestampUpdated) {
  // checks that user activity timestamp is updated during Mount() and
  // periodically while mounted, other Keyset fields remains the same
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  InsertTestUsers(&kDefaultUsers[9], 1);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  EXPECT_CALL(platform_,
              CreateDirectory(AnyOf(mount_->GetNewUserPath(user->username),
                                    StartsWith(kImageDir))))
    .WillRepeatedly(Return(true));

  user->InjectKeyset(&platform_, false);
  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  // Mount()
  MountError error;
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _))
      .Times(4)
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(mount_->MountCryptohome(up, Mount::MountArgs(), &error));

  // Update the timestamp. Normally it is called in MountTaskMount::Run() in
  // background but here in the test we must call it manually.
  static const int kMagicTimestamp = 123;
  chromeos::Blob updated_keyset;
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillRepeatedly(DoAll(SaveArg<1>(&updated_keyset), Return(true)));
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::FromInternalValue(kMagicTimestamp)));
  mount_->UpdateCurrentUserActivityTimestamp(0);
  SerializedVaultKeyset serialized1;
  ASSERT_TRUE(serialized1.ParseFromArray(updated_keyset.data(),
                                         updated_keyset.size()));

  // Check that last activity timestamp is updated.
  ASSERT_TRUE(serialized1.has_last_activity_timestamp());
  EXPECT_EQ(kMagicTimestamp, serialized1.last_activity_timestamp());

  // Unmount the user. This must update user's activity timestamps.
  static const int kMagicTimestamp2 = 234;
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::FromInternalValue(kMagicTimestamp2)));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .Times(5)
      .WillRepeatedly(Return(true));
  mount_->UnmountCryptohome();
  SerializedVaultKeyset serialized2;
  ASSERT_TRUE(serialized2.ParseFromArray(updated_keyset.data(),
                                         updated_keyset.size()));
  ASSERT_TRUE(serialized2.has_last_activity_timestamp());
  EXPECT_EQ(kMagicTimestamp2, serialized2.last_activity_timestamp());

  // Update timestamp again, after user is unmounted. User's activity
  // timestamp must not change this.
  mount_->UpdateCurrentUserActivityTimestamp(0);
  SerializedVaultKeyset serialized3;
  ASSERT_TRUE(serialized3.ParseFromArray(updated_keyset.data(),
                                         updated_keyset.size()));
  ASSERT_TRUE(serialized3.has_last_activity_timestamp());
  EXPECT_EQ(serialized3.has_last_activity_timestamp(),
            serialized2.has_last_activity_timestamp());
}

TEST_F(MountTest, MountForUserOrderingTest) {
  // Checks that mounts made with MountForUser/BindForUser are undone in the
  // right order.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
    .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());
  UserSession session;
  SecureBlob salt;
  salt.assign('A', 16);
  session.Init(salt);
  UsernamePasskey up("username", SecureBlob("password"));
  EXPECT_TRUE(session.SetUser(up));

  std::string src = "/src";
  std::string dest0 = "/dest/foo";
  std::string dest1 = "/dest/bar";
  std::string dest2 = "/dest/baz";
  {
    InSequence sequence;
    EXPECT_CALL(platform_, Mount(src, dest0, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(src, dest1))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Mount(src, dest2, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest2, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest1, _, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest0, _, _))
        .WillOnce(Return(true));

    EXPECT_TRUE(mount_->MountForUser(&session, src, dest0, "", ""));
    EXPECT_TRUE(mount_->BindForUser(&session, src, dest1));
    EXPECT_TRUE(mount_->MountForUser(&session, src, dest2, "", ""));
    mount_->UnmountAllForUser(&session);
    EXPECT_FALSE(mount_->UnmountForUser(&session));
  }
}

TEST_F(MountTest, LockboxGetsFinalized) {
  StrictMock<MockBootLockbox> lockbox;
  mount_->set_boot_lockbox(&lockbox);
  ASSERT_TRUE(DoMountInit());
  EXPECT_CALL(lockbox, FinalizeBoot()).Times(2).WillRepeatedly(Return(true));
  UsernamePasskey up("username", SecureBlob("password"));
  Mount::MountArgs args;
  MountError error = MOUNT_ERROR_NONE;
  mount_->MountCryptohome(up, args, &error);
  mount_->MountGuestCryptohome();
}

// Test setup that initially has no cryptohomes.
const TestUserInfo kNoUsers[] = {
  {"user0@invalid.domain", "zero", false},
  {"user1@invalid.domain", "odin", false},
  {"user2@invalid.domain", "dwaa", false},
  {"owner@invalid.domain", "1234", false},
};
const int kNoUserCount = arraysize(kNoUsers);

// Test setup that initially has a cryptohome for the owner only.
const TestUserInfo kOwnerOnlyUsers[] = {
  {"user0@invalid.domain", "zero", false},
  {"user1@invalid.domain", "odin", false},
  {"user2@invalid.domain", "dwaa", false},
  {"owner@invalid.domain", "1234", true},
};
const int kOwnerOnlyUserCount = arraysize(kOwnerOnlyUsers);

// Test setup that initially has cryptohomes for all users.
const TestUserInfo kAlternateUsers[] = {
  {"user0@invalid.domain", "zero", true},
  {"user1@invalid.domain", "odin", true},
  {"user2@invalid.domain", "dwaa", true},
  {"owner@invalid.domain", "1234", true},
};
const int kAlternateUserCount = arraysize(kAlternateUsers);

class AltImageTest : public MountTest {
 public:
  AltImageTest() {}
  ~AltImageTest() {
    MountTest::TearDown();
  }

  void SetUpAltImage(const TestUserInfo *users, int user_count) {
    // Set up fresh users.
    MountTest::SetUp();
    InsertTestUsers(users, user_count);

    EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
    EXPECT_TRUE(DoMountInit());
  }

  bool StoreSerializedKeyset(const SerializedVaultKeyset& serialized,
                             TestUser *user) {
    user->credentials.resize(serialized.ByteSize());
    serialized.SerializeWithCachedSizesToArray(
        static_cast<google::protobuf::uint8*>(&user->credentials[0]));
    return true;
  }

  // Set the user with specified |key_file| old.
  bool SetUserTimestamp(TestUser* user, base::Time timestamp) {
    SerializedVaultKeyset serialized;
    if (!LoadSerializedKeyset(user->credentials, &serialized)) {
      LOG(ERROR) << "Failed to parse keyset for "
                 << user->username;
      return false;
    }
    serialized.set_last_activity_timestamp(timestamp.ToInternalValue());
    bool ok = StoreSerializedKeyset(serialized, user);
    if (!ok) {
      LOG(ERROR) << "Failed to serialize new timestamp'd keyset for "
                 << user->username;
    }
    return ok;
  }

  void PrepareHomedirs(bool populate_cache,
                       bool populate_gcache,
                       bool inject_keyset,
                       const std::vector<int>* delete_vaults,
                       const std::vector<int>* mounted_vaults) {
    bool populate_vaults = (vaults_.size() == 0);
    // const string contents = "some encrypted contents";
    for (int user = 0; user != static_cast<int>(helper_.users.size()); user++) {
      // Let their Cache dirs be filled with some data.
      // Guarded to keep this function reusable.
      if (populate_vaults) {
        EXPECT_CALL(platform_,
            DirectoryExists(StartsWith(helper_.users[user].base_path)))
          .WillRepeatedly(Return(true));
        vaults_.push_back(helper_.users[user].base_path);
      }
      bool delete_user = false;
      if (delete_vaults && delete_vaults->size() != 0) {
        if (std::find(delete_vaults->begin(), delete_vaults->end(), user) !=
            delete_vaults->end())
          delete_user = true;
      }
      bool mounted_user = false;
      if (mounted_vaults && mounted_vaults->size() != 0) {
        if (std::find(mounted_vaults->begin(), mounted_vaults->end(), user) !=
            mounted_vaults->end())
          mounted_user = true;
      }

      // <vault>/user/Cache
      {
        InSequence two_tests;
        if (populate_cache && !mounted_user) {
          InSequence s;
          // Create a contents to be deleted.
          MockFileEnumerator* files = new MockFileEnumerator();
          EXPECT_CALL(platform_,
              GetFileEnumerator(StringPrintf("%s/%s",
                                  helper_.users[user].user_vault_path.c_str(),
                                  kCacheDir),
                                false, _))
            .WillOnce(Return(files));
          EXPECT_CALL(*files, Next())
            .WillOnce(Return("MOCK/cached_file"));
          EXPECT_CALL(platform_, DeleteFile("MOCK/cached_file", _))
            .WillOnce(Return(true));
          EXPECT_CALL(*files, Next())
            .WillOnce(Return("MOCK/cached_subdir"));
          EXPECT_CALL(platform_, DeleteFile("MOCK/cached_subdir", true))
            .WillOnce(Return(true));
          EXPECT_CALL(*files, Next())
            .WillRepeatedly(Return(""));
        }
        if (populate_gcache && !mounted_user) {
          InSequence s;
          // Create a contents to be deleted.
          MockFileEnumerator* files = new MockFileEnumerator();
          EXPECT_CALL(platform_,
              GetFileEnumerator(StartsWith(
                                  StringPrintf("%s/%s",
                                    helper_.users[user].user_vault_path.c_str(),
                                    kGCacheDir)),
                                false, _))
            .WillOnce(Return(files));
          EXPECT_CALL(*files, Next())
            .WillOnce(Return("MOCK/gcached_file"));
          EXPECT_CALL(platform_, DeleteFile("MOCK/gcached_file", _))
            .WillOnce(Return(true));
          EXPECT_CALL(*files, Next())
            .WillOnce(Return("MOCK/gcached_subdir"));
          EXPECT_CALL(platform_, DeleteFile("MOCK/gcached_subdir", true))
            .WillOnce(Return(true));
          EXPECT_CALL(*files, Next())
            .WillRepeatedly(Return(""));
        }
      }
      // After Cache & GCache are depleted. Users are deleted. To do so cleanly,
      // their keysets timestamps are read into an in-memory.
      if (inject_keyset && !mounted_user) {
        helper_.users[user].InjectKeyset(&platform_, false);
        key_indices_.push_back(0);
        EXPECT_CALL(homedirs_,
                    GetVaultKeysets(helper_.users[user].obfuscated_username,
                                    _))
            .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices_),
                           Return(true)));
      }
      if (delete_user) {
        EXPECT_CALL(platform_,
            DeleteFile(helper_.users[user].base_path, true))
          .WillOnce(Return(true));
      }
    }
  }

  std::vector<std::string> vaults_;

 protected:
  std::vector<int> key_indices_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AltImageTest);
};

class EphemeralNoUserSystemTest : public AltImageTest {
 public:
  EphemeralNoUserSystemTest() { }

  void SetUp() {
    SetUpAltImage(kNoUsers, kNoUserCount);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralNoUserSystemTest);
};

TEST_F(EphemeralNoUserSystemTest, OwnerUnknownMountCreateTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a regular vault is created and mounted.
  set_policy(false, "", true);

  TestUser *user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(user->image_path))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
    .WillRepeatedly(Return(true));
  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(user->credentials),
                          Return(true)));
  EXPECT_CALL(platform_, DirectoryExists(StartsWith(user->user_vault_path)))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .Times(0);
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up,
                                      mount_args,
                                      &error));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

// TODO(wad) Duplicate these tests with multiple mounts instead of one.

TEST_F(EphemeralNoUserSystemTest, EnterpriseMountNoCreateTest) {
  // Checks that when a device is enterprise enrolled, a tmpfs cryptohome is
  // mounted and no regular vault is created.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  TestUser *user = &helper_.users[0];

  // Always removes non-owner cryptohomes.
  std::vector<std::string> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Stat(_, _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path))
    .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));

  // Make sure it's a tmpfs mount until we move to ephemeral key use.
  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);

  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  UsernamePasskey up(user->username, user->passkey);
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));
}

TEST_F(EphemeralNoUserSystemTest, OwnerUnknownMountEnsureEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a mount request with the |ensure_ephemeral| flag set fails.
  TestUser *user = &helper_.users[0];

  EXPECT_CALL(platform_, Mount(_, _, _, _)).Times(0);

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  UsernamePasskey up(user->username, user->passkey);
  ASSERT_FALSE(mount_->MountCryptohome(up, mount_args, &error));
  ASSERT_EQ(MOUNT_ERROR_FATAL, error);
}

TEST_F(EphemeralNoUserSystemTest, EnterpriseMountEnsureEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |ensure_ephemeral| flag set causes a tmpfs cryptohome to be mounted and no
  // regular vault to be created.
  set_policy(true, "", false);
  mount_->set_enterprise_owned(true);
  TestUser *user = &helper_.users[0];

  // Always removes non-owner cryptohomes.
  std::vector<std::string> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Stat(_, _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path))
    .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);

  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  UsernamePasskey up(user->username, user->passkey);
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));

  EXPECT_CALL(platform_, Unmount(StartsWith("/home/chronos/u-"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/user/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/root/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, ClearUserKeyring())
      .WillRepeatedly(Return(0));
  EXPECT_TRUE(mount_->UnmountCryptohome());
}

class EphemeralOwnerOnlySystemTest : public AltImageTest {
 public:
  EphemeralOwnerOnlySystemTest() { }

  void SetUp() {
    SetUpAltImage(kOwnerOnlyUsers, kOwnerOnlyUserCount);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralOwnerOnlySystemTest);
};

TEST_F(EphemeralOwnerOnlySystemTest, MountNoCreateTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a tmpfs cryptohome is mounted and no regular vault is created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, true);
  UsernamePasskey up(user->username, user->passkey);

  // Always removes non-owner cryptohomes.
  std::vector<std::string> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(owner_only), Return(true)));

  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Stat(_, _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path))
    .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));

  EXPECT_CALL(platform_, Unmount(StartsWith("/home/chronos/u-"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/user/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/root/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, ClearUserKeyring())
      .WillRepeatedly(Return(0));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralOwnerOnlySystemTest, NonOwnerMountEnsureEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |ensure_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted and no regular vault to be created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, false);
  UsernamePasskey up(user->username, user->passkey);

  // Always removes non-owner cryptohomes.
  std::vector<std::string> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(owner_only), Return(true)));

  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Stat(_, _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path))
    .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillOnce(Return(true))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));
}

TEST_F(EphemeralOwnerOnlySystemTest, OwnerMountEnsureEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for the owner with the |ensure_ephemeral| flag set fails.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);
  UsernamePasskey up(owner->username, owner->passkey);

  EXPECT_CALL(platform_, Mount(_, _, _, _)).Times(0);

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  ASSERT_FALSE(mount_->MountCryptohome(up, mount_args, &error));
  ASSERT_EQ(MOUNT_ERROR_FATAL, error);
}

class EphemeralExistingUserSystemTest : public AltImageTest {
 public:
  EphemeralExistingUserSystemTest() { }

  void SetUp() {
    SetUpAltImage(kAlternateUsers, kAlternateUserCount);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralExistingUserSystemTest);
};

TEST_F(EphemeralExistingUserSystemTest, OwnerUnknownMountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while mounting.
  set_policy(false, "", true);
  TestUser* user = &helper_.users[0];

  // No c-homes will be removed.  The rest of the mocking just gets us to
  // Mount().
  std::vector<TestUser>::iterator it;
  for (it = helper_.users.begin(); it != helper_.users.end(); ++it)
    it->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);

  std::vector<std::string> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
    .WillOnce(DoAll(SetArgumentPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Stat(_, _))
    .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
    .Times(2)
    .WillRepeatedly(Return(0));
  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));

  EXPECT_CALL(platform_, CreateDirectory(user->vault_path))
    .Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));

  std::vector<int> key_indices;
  key_indices.push_back(0);
  EXPECT_CALL(homedirs_, GetVaultKeysets(user->obfuscated_username, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(key_indices),
                          Return(true)));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .Times(0);
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  user->InjectKeyset(&platform_, false);
  UsernamePasskey up(user->username, user->passkey);
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(EndsWith("/mount"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/chronos/u-"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/user/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/root/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, ClearUserKeyring())
      .WillRepeatedly(Return(0));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, EnterpriseMountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while mounting.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(false, false, true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<std::string> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillOnce(DoAll(SetArgumentPointee<2>(vaults_), Return(true)))
    // Don't re-delete on Unmount.
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home/chronos",
                 mount_->GetNewUserPath(user->username)),
           _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home",
                 "/home/root",
                 chromeos::cryptohome::home::GetRootPath(
                     user->username).value(),
                 "/home/user",
                 chromeos::cryptohome::home::GetUserPath(
                     user->username).value()),
           _))
    .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_, kImageDir, false);
  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_, DirectoryExists(StartsWith(user->user_mount_path)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/chronos/u-"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/user/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/root/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, ClearUserKeyring())
      .WillRepeatedly(Return(0));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, MountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all non-owner cryptohomes are removed while mounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);  // Mounting user shouldn't use be persistent.
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  // Expect all users but the owner to be removed.
  PrepareHomedirs(false, false, true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<std::string> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillOnce(DoAll(SetArgumentPointee<2>(vaults_), Return(true)))
    // Don't re-delete on Unmount.
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home/chronos",
                 mount_->GetNewUserPath(user->username)),
           _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home",
                 "/home/root",
                 chromeos::cryptohome::home::GetRootPath(
                   user->username).value(),
                 "/home/user",
                 chromeos::cryptohome::home::GetUserPath(
                   user->username).value()),
           _))
    .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_, kImageDir, false);
  user->InjectUserPaths(&platform_, chronos_uid_, chronos_gid_,
                        shared_gid_, kDaemonGid);
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_, DirectoryExists(StartsWith(user->user_mount_path)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/chronos/u-"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/user/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(StartsWith("/home/root/"), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount("/home/chronos/user", _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, ClearUserKeyring())
      .WillRepeatedly(Return(0));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, OwnerUnknownUnmountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while unmounting.
  set_policy(false, "", true);
  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, EnterpriseUnmountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while unmounting.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(false, false, false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, UnmountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all stale cryptohomes are removed while unmounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);
  // All users but the owner.
  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  PrepareHomedirs(false, false, false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, ClearUserKeyring())
    .WillOnce(Return(0));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_F(EphemeralExistingUserSystemTest, NonOwnerMountEnsureEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |ensure_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted, even if a regular vault exists for
  // the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);
  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  PrepareHomedirs(false, false, true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home/chronos", mount_->GetNewUserPath(user->username)), _))
     .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home",
                 "/home/root",
                 chromeos::cryptohome::home::GetRootPath(
                   user->username).value(),
                 "/home/user",
                 chromeos::cryptohome::home::GetUserPath(
                   user->username).value()),
           _))
    .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_, DirectoryExists(StartsWith(user->user_mount_path)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  EXPECT_CALL(platform_, FileExists(StartsWith("/home/chronos/user")))
    .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_, kImageDir, false);

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));
}

TEST_F(EphemeralExistingUserSystemTest, EnterpriseMountEnsureEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |ensure_ephemeral| flag set causes a tmpfs cryptohome to be mounted, even
  // if a regular vault exists for the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  set_policy(true, "", false);
  mount_->set_enterprise_owned(true);

  TestUser* user = &helper_.users[0];
  UsernamePasskey up(user->username, user->passkey);

  // Mounting user vault won't be deleted, but tmpfs mount should still be
  // used.
  PrepareHomedirs(false, false, true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMountedWith(_, _))
    .WillRepeatedly(Return(false));
  std::vector<std::string> empty;
  EXPECT_CALL(platform_,
      EnumerateDirectoryEntries(AnyOf("/home/root/",
                                      "/home/user/"), _, _))
    .WillRepeatedly(DoAll(SetArgumentPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home/chronos",
                 mount_->GetNewUserPath(user->username)),
           _))
    .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
      Stat(AnyOf("/home",
                 "/home/root",
                 chromeos::cryptohome::home::GetRootPath(
                     user->username).value(),
                 "/home/user",
                 chromeos::cryptohome::home::GetUserPath(
                     user->username).value()),
           _))
    .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_, DirectoryExists(StartsWith(user->user_mount_path)))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
    .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  EXPECT_CALL(platform_, FileExists(StartsWith("/home/chronos/user")))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_, kImageDir, false);

  EXPECT_CALL(platform_, Mount(_, _, _, _))
      .Times(0);
  EXPECT_CALL(platform_, IsDirectoryMounted("test_image_dir/skeleton"))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount(_, _, kEphemeralMountType, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount("test_image_dir/skeleton", _, _))
      .WillOnce(Return(true));  // Scope mount
  EXPECT_CALL(platform_, IsDirectoryMounted("/home/chronos/user"))
    .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_, Bind(_, _))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args;
  mount_args.create_if_missing = true;
  mount_args.ensure_ephemeral = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(up, mount_args, &error));
}

TEST_F(EphemeralNoUserSystemTest, MountGuestUserDir) {
  struct stat fake_root_st;
  fake_root_st.st_uid = 0;
  fake_root_st.st_gid = 0;
  fake_root_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat("/home", _))
    .Times(3)
    .WillRepeatedly(DoAll(SetArgumentPointee<1>(fake_root_st),
                          Return(true)));
  EXPECT_CALL(platform_, Stat("/home/root", _))
    .WillOnce(DoAll(SetArgumentPointee<1>(fake_root_st),
                    Return(true)));
  EXPECT_CALL(platform_, Stat(StartsWith("/home/root/"), _))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, Stat("/home/user", _))
    .WillOnce(DoAll(SetArgumentPointee<1>(fake_root_st),
                    Return(true)));
  EXPECT_CALL(platform_, Stat(StartsWith("/home/user/"), _))
    .WillOnce(Return(false));
  struct stat fake_user_st;
  fake_user_st.st_uid = chronos_uid_;
  fake_user_st.st_gid = chronos_gid_;
  fake_user_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat("/home/chronos", _))
    .WillOnce(DoAll(SetArgumentPointee<1>(fake_user_st),
                    Return(true)));
  EXPECT_CALL(platform_, CreateDirectory(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(_))
    .WillOnce(Return(true))
    .WillOnce(Return(false))
    .WillOnce(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_))
    .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Mount("guestfs", "test_image_dir/skeleton", _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Mount("guestfs", StartsWith("/home/root/"), _, _))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind("test_image_dir/skeleton",
                              StartsWith("/home/user/")))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(StartsWith("/home/user/"),
                              "/home/chronos/user"))
    .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(StartsWith("/home/user/"),
                              StartsWith("/home/chronos/u-")))
    .WillOnce(Return(true));

  ASSERT_TRUE(mount_->MountGuestCryptohome());
}

}  // namespace cryptohome
