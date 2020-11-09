// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for Mount.

#include "cryptohome/mount.h"

#include <memory>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>  // For memset(), memcpy()
#include <sys/types.h>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <gtest/gtest.h>
#include <policy/libpolicy.h>
#include <policy/mock_device_policy.h>

#include "cryptohome/bootlockbox/mock_boot_lockbox.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/make_tests.h"
#include "cryptohome/mock_chaps_client_factory.h"
#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/mock_vault_keyset.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/timestamp.pb.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using brillo::SecureBlob;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AnyOf;
using ::testing::AnyOfArray;
using ::testing::DoAll;
using ::testing::EndsWith;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StartsWith;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::Unused;
using ::testing::WithArgs;

namespace {

const FilePath kImageDir("/home/.shadow");
const FilePath kImageSaltFile = kImageDir.Append("salt");
const FilePath kSkelDir = kImageDir.Append("skel");
const FilePath kLoopDevice("/dev/loop7");

const gid_t kDaemonGid = 400;  // TODO(wad): expose this in mount.h

}  // namespace

namespace cryptohome {

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
Tpm::TpmRetryAction TpmPassthroughSealWithAuthorization(
    uint32_t _key,
    const SecureBlob& plaintext,
    Unused,
    Unused,
    SecureBlob* ciphertext) {
  ciphertext->resize(plaintext.size());
  memcpy(ciphertext->data(), plaintext.data(), plaintext.size());
  return Tpm::kTpmRetryNone;
}

Tpm::TpmRetryAction TpmPassthroughDecrypt(uint32_t _key,
                                          const SecureBlob& ciphertext,
                                          Unused,
                                          Unused,
                                          SecureBlob* plaintext) {
  plaintext->resize(ciphertext.size());
  memcpy(plaintext->data(), ciphertext.data(), ciphertext.size());
  return Tpm::kTpmRetryNone;
}

std::string HexDecode(const std::string& hex) {
  std::vector<uint8_t> output;
  CHECK(base::HexStringToBytes(hex, &output));
  return std::string(output.begin(), output.end());
}

class MountTest
    : public ::testing::TestWithParam<bool /* should_test_ecryptfs */> {
 public:
  MountTest() : crypto_(&platform_) {}
  virtual ~MountTest() {}

  void SetUp() {
    // Populate the system salt
    helper_.SetUpSystemSalt();
    helper_.InjectSystemSalt(&platform_, kImageSaltFile);

    crypto_.set_tpm(&tpm_);
    crypto_.set_use_tpm(false);
    homedirs_.set_use_tpm(false);

    platform_.GetFake()->SetStandardUsersAndGroups();

    user_timestamp_cache_.reset(new UserOldestActivityTimestampCache());
    mount_ = new Mount();
    mount_->set_homedirs(&homedirs_);
    mount_->set_use_tpm(false);
    mount_->set_shadow_root(kImageDir);
    mount_->set_skel_source(kSkelDir);
    mount_->set_chaps_client_factory(&chaps_client_factory_);
    // Perform mounts in-process.
    mount_->set_mount_guest_session_out_of_process(false);
    mount_->set_mount_guest_session_non_root_namespace(false);
    homedirs_.set_crypto(&crypto_);
    homedirs_.set_platform(&platform_);
    homedirs_.set_shadow_root(kImageDir);
    EXPECT_TRUE(homedirs_.GetSystemSalt(nullptr /* blob */));
    set_policy(false, "", false);
  }

  void TearDown() {
    mount_ = nullptr;
    helper_.TearDownSystemSalt();
  }

  void InsertTestUsers(const TestUserInfo* user_info_list, int count) {
    helper_.InitTestData(kImageDir, user_info_list, static_cast<size_t>(count),
                         ShouldTestEcryptfs());
  }

  bool DoMountInit() {
    return mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get());
  }

  bool LoadSerializedKeyset(const brillo::Blob& contents,
                            cryptohome::SerializedVaultKeyset* serialized) {
    CHECK_NE(contents.size(), 0U);
    return serialized->ParseFromArray(contents.data(), contents.size());
  }

  bool StoreSerializedKeyset(const SerializedVaultKeyset& serialized,
                             TestUser* user) {
    user->credentials.resize(serialized.ByteSizeLong());
    serialized.SerializeWithCachedSizesToArray(
        static_cast<google::protobuf::uint8*>(&user->credentials[0]));
    return true;
  }

  void GetKeysetBlob(const SerializedVaultKeyset& serialized,
                     SecureBlob* blob) {
    SecureBlob local_wrapped_keyset(serialized.wrapped_keyset().length());
    serialized.wrapped_keyset().copy(local_wrapped_keyset.char_data(),
                                     serialized.wrapped_keyset().length(), 0);
    blob->swap(local_wrapped_keyset);
  }

  void set_policy(bool owner_known,
                  const std::string& owner,
                  bool ephemeral_users_enabled) {
    policy::MockDevicePolicy* device_policy = new policy::MockDevicePolicy();
    EXPECT_CALL(*device_policy, LoadPolicy())
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*device_policy, GetOwner(_))
        .WillRepeatedly(SetOwner(owner_known, owner));
    EXPECT_CALL(*device_policy, GetEphemeralUsersEnabled(_))
        .WillRepeatedly(SetEphemeralUsersEnabled(ephemeral_users_enabled));
    mount_->set_policy_provider(new policy::PolicyProvider(
        std::unique_ptr<policy::MockDevicePolicy>(device_policy)));
  }

  // Returns true if the test is running for eCryptfs, false if for dircrypto.
  bool ShouldTestEcryptfs() const { return GetParam(); }

  Mount::MountArgs GetDefaultMountArgs() const {
    Mount::MountArgs args;
    args.create_as_ecryptfs = ShouldTestEcryptfs();
    return args;
  }

  // Sets expectations for cryptohome key setup.
  void ExpectCryptohomeKeySetup(const TestUser& user) {
    if (ShouldTestEcryptfs()) {
      ExpectCryptohomeKeySetupForEcryptfs(user);
    } else {
      ExpectCryptohomeKeySetupForDircrypto(user);
    }
  }

  // Sets expectations for cryptohome key setup for ecryptfs.
  void ExpectCryptohomeKeySetupForEcryptfs(const TestUser& user) {
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
        .Times(2)
        .WillRepeatedly(Return(true));
  }

  // Sets expectations for cryptohome key setup for dircrypto.
  void ExpectCryptohomeKeySetupForDircrypto(const TestUser& user) {
    EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, SetDirCryptoKey(user.vault_mount_path, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, InvalidateDirCryptoKey(_, kImageDir))
        .WillRepeatedly(Return(true));
  }

  void ExpectCryptohomeMountShadowOnly(const TestUser& user) {
    ExpectCryptohomeKeySetup(user);
    if (ShouldTestEcryptfs()) {
      EXPECT_CALL(platform_, Mount(user.vault_path, user.vault_mount_path,
                                   "ecryptfs", kDefaultMountFlags, _))
          .WillOnce(Return(true));
    }
    EXPECT_CALL(platform_, CreateDirectory(user.vault_mount_path))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, IsDirectoryMounted(user.vault_mount_path))
        .WillOnce(Return(false));
  }

  // Sets expectations for cryptohome mount.
  void ExpectCryptohomeMount(const TestUser& user) {
    ExpectCryptohomeKeySetup(user);
    ExpectDaemonStoreMounts(user, false /* ephemeral_mount */);
    if (ShouldTestEcryptfs()) {
      EXPECT_CALL(platform_, Mount(user.vault_path, user.vault_mount_path,
                                   "ecryptfs", kDefaultMountFlags, _))
          .WillOnce(Return(true));
    }
    EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
        .WillRepeatedly(Return(false));
    EXPECT_CALL(platform_, CreateDirectory(user.vault_mount_path))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                CreateDirectory(MountHelper::GetNewUserPath(user.username)))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, IsDirectoryMounted(user.vault_mount_path))
        .WillOnce(Return(false));
    EXPECT_CALL(platform_, IsDirectoryMounted(FilePath("/home/chronos/user")))
        .WillOnce(Return(false));

    EXPECT_CALL(platform_,
                Bind(user.user_vault_mount_path, user.user_mount_path))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Bind(user.user_vault_mount_path, user.legacy_user_mount_path))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(user.user_vault_mount_path,
                                MountHelper::GetNewUserPath(user.username)))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_,
                Bind(user.root_vault_mount_path, user.root_mount_path))
        .WillOnce(Return(true));
    ExpectDownloadsBindMounts(user);
    EXPECT_CALL(platform_, RestoreSELinuxContexts(
                               base::FilePath(user.vault_mount_path), true))
        .WillOnce(Return(true));
  }

  void ExpectDownloadsBindMounts(const TestUser& user) {
    // Mounting Downloads to MyFiles/Downloads in:
    //   - /home/chronos/u-<hash>
    //   - /home/user/<hash>
    //   - /home/chronos/user
    FilePath user_dirs[] = {
        MountHelper::GetNewUserPath(user.username),
        brillo::cryptohome::home::GetUserPath(user.username),
        FilePath("/home/chronos/user"),
    };

    EXPECT_CALL(
        platform_,
        IsDirectoryMounted(FilePath("/home/chronos/user/MyFiles/Downloads")))
        .WillOnce(Return(false));
    for (auto const& home : user_dirs) {
      auto downloads_path = home.Append("Downloads");
      auto downloads_in_myfiles = home.Append("MyFiles").Append("Downloads");

      EXPECT_CALL(platform_, DirectoryExists(home)).WillOnce(Return(true));
      EXPECT_CALL(platform_, DirectoryExists(downloads_path))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DirectoryExists(downloads_in_myfiles))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles))
          .WillOnce(Return(true));

      NiceMock<MockFileEnumerator>* in_myfiles_download_enumerator =
          new NiceMock<MockFileEnumerator>();
      EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
          .WillOnce(Return(in_myfiles_download_enumerator));
    }
  }

  void ExpectDownloadsUnmounts(const TestUser& user) {
    // Mounting Downloads to MyFiles/Downloads in:
    //   - /home/chronos/u-<hash>
    //   - /home/user/<hash>
    //   - /home/chronos/user
    FilePath user_dirs[] = {
        MountHelper::GetNewUserPath(user.username),
        brillo::cryptohome::home::GetUserPath(user.username),
        FilePath("/home/chronos/user"),
    };
    for (auto const& home : user_dirs) {
      EXPECT_CALL(platform_,
                  Unmount(home.Append("MyFiles").Append("Downloads"), _, _))
          .WillOnce(Return(true));
    }
  }

  void ExpectEphemeralCryptohomeMount(const TestUser& user) {
    EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
        .WillOnce(Return(true));
    const FilePath ephemeral_filename =
        MountHelper::GetEphemeralSparseFile(user.obfuscated_username);
    EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
        .WillOnce(Return(kLoopDevice));
    EXPECT_CALL(platform_,
                FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, Mount(kLoopDevice, _, kEphemeralMountType,
                                 kDefaultMountFlags, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                SetSELinuxContext(Property(&FilePath::value,
                                           StartsWith(kEphemeralCryptohomeDir)),
                                  cryptohome::kEphemeralCryptohomeRootContext))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, IsDirectoryMounted(FilePath("/home/chronos/user")))
        .WillOnce(Return(false));  // first mount
    EXPECT_CALL(
        platform_,
        IsDirectoryMounted(FilePath("/home/chronos/user/MyFiles/Downloads")))
        .WillOnce(Return(false));
    EXPECT_CALL(platform_, Bind(_, _)).WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
    EXPECT_CALL(
        platform_,
        GetFileEnumerator(
            Property(&FilePath::value, EndsWith("MyFiles/Downloads")), _, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
    EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, CreateDirectory(user.vault_path)).Times(0);
    EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, SetOwnership(_, _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
    ExpectDaemonStoreMounts(user, true /* ephemeral_mount */);
  }

  // Sets expectations for MountHelper::MountDaemonStoreDirectories. In
  // particular, sets up |platform_| to pretend that all daemon store
  // directories exists, so that they're all mounted. Without calling this
  // method, daemon store directories are pretended to not exist.
  void ExpectDaemonStoreMounts(const TestUser& user, bool ephemeral_mount) {
    // Return a mock daemon store directory in /etc/daemon-store.
    constexpr char kDaemonName[] = "mock-daemon";
    constexpr uid_t kDaemonUid = 123;
    constexpr gid_t kDaemonGid = 234;
    base::stat_wrapper_t stat_data = {};
    stat_data.st_mode = S_IFDIR;
    stat_data.st_uid = kDaemonUid;
    stat_data.st_gid = kDaemonGid;
    const base::FilePath daemon_store_base_dir(kEtcDaemonStoreBaseDir);
    const FileEnumerator::FileInfo daemon_info(
        daemon_store_base_dir.AppendASCII(kDaemonName), stat_data);
    NiceMock<MockFileEnumerator>* daemon_enumerator =
        new NiceMock<MockFileEnumerator>();
    daemon_enumerator->entries_.push_back(daemon_info);
    EXPECT_CALL(platform_, GetFileEnumerator(daemon_store_base_dir, false,
                                             base::FileEnumerator::DIRECTORIES))
        .WillOnce(Return(daemon_enumerator));

    const FilePath run_daemon_store_path =
        FilePath(kRunDaemonStoreBaseDir).Append(kDaemonName);

    EXPECT_CALL(platform_, DirectoryExists(run_daemon_store_path))
        .WillOnce(Return(true));

    const FilePath root_home = ephemeral_mount ? user.root_ephemeral_mount_path
                                               : user.root_vault_mount_path;
    const FilePath mount_source = root_home.Append(kDaemonName);
    const FilePath mount_target =
        run_daemon_store_path.Append(user.obfuscated_username);

    EXPECT_CALL(platform_, CreateDirectory(mount_source))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, CreateDirectory(mount_target))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, SetOwnership(mount_source, stat_data.st_uid,
                                        stat_data.st_gid, false))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, SetPermissions(mount_source, stat_data.st_mode))
        .WillOnce(Return(true));

    EXPECT_CALL(platform_, Bind(mount_source, mount_target))
        .WillOnce(Return(true));
  }

  void ExpectCryptohomeRemoval(const TestUser& user) {
    EXPECT_CALL(platform_, DeleteFile(user.base_path, true)).Times(1);
    EXPECT_CALL(platform_, DeleteFile(user.user_mount_path, true)).Times(1);
    EXPECT_CALL(platform_, DeleteFile(user.root_mount_path, true)).Times(1);
  }

 protected:
  // Protected for trivial access.
  MakeTests helper_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockTpm> tpm_;
  NiceMock<MockTpmInit> tpm_init_;
  Crypto crypto_;
  HomeDirs homedirs_;
  MockChapsClientFactory chaps_client_factory_;
  std::unique_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;
  scoped_refptr<Mount> mount_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

 private:
  DISALLOW_COPY_AND_ASSIGN(MountTest);
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs, MountTest, ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto, MountTest, ::testing::Values(false));

TEST_P(MountTest, BadInitTest) {
  // Create a Mount instance that points to a bad shadow root.
  mount_->set_shadow_root(FilePath("/dev/null"));

  SecureBlob passkey;
  cryptohome::Crypto::PasswordToPasskey(kDefaultUsers[0].password,
                                        helper_.system_salt, &passkey);

  // Shadow root creation should fail.
  EXPECT_CALL(platform_, DirectoryExists(FilePath("/dev/null")))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(FilePath("/dev/null")))
      .WillRepeatedly(Return(false));
  // Salt creation failure because shadow_root is bogus.
  EXPECT_CALL(platform_, FileExists(FilePath("/dev/null/salt")))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, WriteSecureBlobToFileAtomicDurable(
                             FilePath("/dev/null/salt"), _, _))
      .WillRepeatedly(Return(false));
  EXPECT_FALSE(mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get()));
}

TEST_P(MountTest, NamespaceCreationPass) {
  mount_->set_mount_guest_session_non_root_namespace(true);
  brillo::ProcessMock* mock_process = platform_.mock_process();
  EXPECT_CALL(*mock_process, Run()).WillOnce(Return(0));
  EXPECT_TRUE(mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get()));
}

TEST_P(MountTest, NamespaceCreationFail) {
  mount_->set_mount_guest_session_non_root_namespace(true);
  brillo::ProcessMock* mock_process = platform_.mock_process();
  EXPECT_CALL(*mock_process, Run()).WillOnce(Return(1));
  EXPECT_FALSE(mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get()));
}

TEST_P(MountTest, MountCryptohomeHasPrivileges) {
  // Check that Mount only works if the mount permission is given.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];
  user->key_data.set_label("my key!");
  user->use_key_data = true;
  // Regenerate the serialized vault keyset.
  user->GenerateCredentials(ShouldTestEcryptfs());
  // Let the legacy key iteration work here.

  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // user exists, so there'll be no skel copy after.

  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      GetDefaultMountArgs(),
                                      /* is_pristine */ false, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  EXPECT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(MountTest, BindMyFilesDownloadsSuccess) {
  FilePath dest_dir("/home/chronos/u-userhash");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  NiceMock<MockFileEnumerator>* in_myfiles_download_enumerator =
      new NiceMock<MockFileEnumerator>();

  // All directories must exist for bind mount succeed.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_in_myfiles))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsMissingUserHome) {
  FilePath dest_dir("/home/chronos/u-userhash");

  // When dest_dir doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(false));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_FALSE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsMissingDownloads) {
  FilePath dest_dir("/home/chronos/u-userhash");
  auto downloads_path = dest_dir.Append("Downloads");

  // When Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(false));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_FALSE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsMissingMyFilesDownloads) {
  FilePath dest_dir("/home/chronos/u-userhash");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");

  // When MyFiles/Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_in_myfiles))
      .WillOnce(Return(false));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_FALSE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsRemoveExistingFiles) {
  FilePath dest_dir("/home/chronos/u-userhash");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  const std::string existing_files[] = {"dir1", "file1"};
  std::vector<FilePath> existing_files_in_download;
  std::vector<FilePath> existing_files_in_myfiles_download;
  auto* in_myfiles_download_enumerator = new NiceMock<MockFileEnumerator>();
  base::stat_wrapper_t stat_file = {};
  stat_file.st_mode = S_IRWXU;
  base::stat_wrapper_t stat_dir = {};
  stat_dir.st_mode = S_IFDIR;

  for (auto base : existing_files) {
    existing_files_in_download.push_back(downloads_path.Append(base));
    existing_files_in_myfiles_download.push_back(
        downloads_in_myfiles.Append(base));
  }
  in_myfiles_download_enumerator->entries_.push_back(
      FileEnumerator::FileInfo(downloads_in_myfiles.Append("dir1"), stat_dir));
  in_myfiles_download_enumerator->entries_.push_back(FileEnumerator::FileInfo(
      downloads_in_myfiles.Append("file1"), stat_file));

  // When MyFiles/Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_in_myfiles))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, FileExists(AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              DeleteFile(AnyOfArray(existing_files_in_myfiles_download), true))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

TEST_P(MountTest, BindMyFilesDownloadsMoveForgottenFiles) {
  FilePath dest_dir("/home/chronos/u-userhash");
  auto downloads_path = dest_dir.Append("Downloads");
  auto downloads_in_myfiles = dest_dir.Append("MyFiles").Append("Downloads");
  const std::string existing_files[] = {"dir1", "file1"};
  std::vector<FilePath> existing_files_in_download;
  std::vector<FilePath> existing_files_in_myfiles_download;
  auto* in_myfiles_download_enumerator = new NiceMock<MockFileEnumerator>();
  base::stat_wrapper_t stat_file = {};
  stat_file.st_mode = S_IRWXU;
  base::stat_wrapper_t stat_dir = {};
  stat_dir.st_mode = S_IFDIR;

  for (auto base : existing_files) {
    existing_files_in_download.push_back(downloads_path.Append(base));
    existing_files_in_myfiles_download.push_back(
        downloads_in_myfiles.Append(base));
  }
  in_myfiles_download_enumerator->entries_.push_back(FileEnumerator::FileInfo(
      downloads_in_myfiles.Append("file1"), stat_file));
  in_myfiles_download_enumerator->entries_.push_back(
      FileEnumerator::FileInfo(downloads_in_myfiles.Append("dir1"), stat_dir));

  // When MyFiles/Downloads doesn't exists BindMyFilesDownloads returns false.
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(downloads_in_myfiles))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetFileEnumerator(downloads_in_myfiles, false, _))
      .WillOnce(Return(in_myfiles_download_enumerator));
  EXPECT_CALL(platform_, FileExists(AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, Move(AnyOfArray(existing_files_in_myfiles_download),
                              AnyOfArray(existing_files_in_download)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(downloads_path, downloads_in_myfiles))
      .WillOnce(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  EXPECT_TRUE(mnt_helper.BindMyFilesDownloads(dest_dir));
}

// A fixture for testing chaps directory checks.
class ChapsDirectoryTest : public ::testing::Test {
 public:
  ChapsDirectoryTest()
      : kBaseDir("/base_chaps_dir"),
        kSaltFile("/base_chaps_dir/auth_data_salt"),
        kDatabaseDir("/base_chaps_dir/database"),
        kDatabaseFile("/base_chaps_dir/database/file"),
        mount_(new Mount()),
        user_timestamp_cache_(new UserOldestActivityTimestampCache()) {
    crypto_.set_platform(&platform_);
    platform_.GetFake()->SetStandardUsersAndGroups();
    mount_->Init(&platform_, &crypto_, user_timestamp_cache_.get());
    mount_->chaps_user_ = fake_platform::kChapsUID;
    mount_->default_access_group_ = fake_platform::kSharedGID;
    // By default, set stats to the expected values.
    InitStat(&base_stat_, 040750, fake_platform::kChapsUID,
             fake_platform::kSharedGID);
    InitStat(&salt_stat_, 0600, fake_platform::kRootUID,
             fake_platform::kRootGID);
    InitStat(&database_dir_stat_, 040750, fake_platform::kChapsUID,
             fake_platform::kSharedGID);
    InitStat(&database_file_stat_, 0640, fake_platform::kChapsUID,
             fake_platform::kSharedGID);
  }

  virtual ~ChapsDirectoryTest() {}

  void SetupFakeChapsDirectory() {
    // Configure the base directory.
    EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, Stat(kBaseDir, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(base_stat_), Return(true)));

    // Configure a fake enumerator.
    MockFileEnumerator* enumerator = new MockFileEnumerator();
    EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
        .WillOnce(Return(enumerator));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kBaseDir, base_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kSaltFile, salt_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kDatabaseDir, database_dir_stat_));
    enumerator->entries_.push_back(
        FileEnumerator::FileInfo(kDatabaseFile, database_file_stat_));
  }

  void SetupFakeChapsDirectoryNoEnumerator() {
    // Configure the base directory.
    EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, Stat(kBaseDir, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(base_stat_), Return(true)));
  }

  bool RunCheck() { return mount_->CheckChapsDirectory(kBaseDir); }

 protected:
  const FilePath kBaseDir;
  const FilePath kSaltFile;
  const FilePath kDatabaseDir;
  const FilePath kDatabaseFile;

  base::stat_wrapper_t base_stat_;
  base::stat_wrapper_t salt_stat_;
  base::stat_wrapper_t database_dir_stat_;
  base::stat_wrapper_t database_file_stat_;

  scoped_refptr<Mount> mount_;
  NiceMock<MockPlatform> platform_;
  NiceMock<MockCrypto> crypto_;
  std::unique_ptr<UserOldestActivityTimestampCache> user_timestamp_cache_;

 private:
  void InitStat(base::stat_wrapper_t* s, mode_t mode, uid_t uid, gid_t gid) {
    memset(s, 0, sizeof(base::stat_wrapper_t));
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
  // Expect basic setup.
  EXPECT_CALL(platform_, CreateDirectory(kBaseDir))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(kBaseDir, 0750))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kBaseDir, fake_platform::kChapsUID,
                                      fake_platform::kSharedGID, true))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, CreateFailure) {
  // Specify directory does not exist.
  EXPECT_CALL(platform_, DirectoryExists(kBaseDir))
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
  base_stat_.st_uid = fake_platform::kRootUID;
  salt_stat_.st_gid = fake_platform::kChapsUID;
  database_dir_stat_.st_gid = fake_platform::kChapsUID;
  database_file_stat_.st_uid = fake_platform::kSharedGID;
  SetupFakeChapsDirectory();
  // Expect corrections.
  EXPECT_CALL(platform_, SetOwnership(kBaseDir, fake_platform::kChapsUID,
                                      fake_platform::kSharedGID, true))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kSaltFile, fake_platform::kRootUID,
                                      fake_platform::kRootGID, true))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kDatabaseDir, fake_platform::kChapsUID,
                                      fake_platform::kSharedGID, true))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(kDatabaseFile, fake_platform::kChapsUID,
                                      fake_platform::kSharedGID, true))
      .WillRepeatedly(Return(true));
  ASSERT_TRUE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadPermsFailure) {
  // Specify some bad perms.
  base_stat_.st_mode = 040700;
  SetupFakeChapsDirectoryNoEnumerator();
  // Expect corrections but fail to apply.
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

TEST_F(ChapsDirectoryTest, FixBadOwnershipFailure) {
  // Specify bad ownership.
  base_stat_.st_uid = fake_platform::kRootUID;
  SetupFakeChapsDirectoryNoEnumerator();
  // Expect corrections but fail to apply.
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(RunCheck());
}

TEST_P(MountTest, MountCryptohome) {
  // checks that cryptohome tries to mount successfully, and tests that the
  // tracked directories are created/replaced as expected
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];

  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // user exists, so there'll be no skel copy after.

  MountError error = MOUNT_ERROR_NONE;
  EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      GetDefaultMountArgs(),
                                      /* is_pristine */ false, &error));
}

TEST_P(MountTest, MountPristineCryptohome) {
  // TODO(wad) Drop NiceMock and replace with InSequence EXPECT_CALL()s.
  // It will complain about creating tracked subdirs, but that is non-fatal.
  EXPECT_TRUE(DoMountInit());
  // Test user at index 12 hasn't been created.
  InsertTestUsers(&kDefaultUsers[12], 1);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_,
              DirectoryExists(AnyOf(user->vault_path, user->vault_mount_path,
                                    user->user_vault_path)))
      .WillOnce(Return(ShouldTestEcryptfs()))
      .WillOnce(Return(false))
      .WillOnce(Return(false));

  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));

  ExpectCryptohomeMount(*user);

  // Fake successful mount to /home/chronos/user/*
  EXPECT_CALL(platform_, FileExists(Property(
                             &FilePath::value,
                             StartsWith(user->legacy_user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->user_vault_mount_path.value()))))
      .WillRepeatedly(Return(true));

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  MountError error = MOUNT_ERROR_NONE;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      mount_args,
                                      /* is_pristine */ true, &error));
  ASSERT_EQ(MOUNT_ERROR_NONE, error);
}

TEST_P(MountTest, UserActivityTimestampUpdated) {
  // checks that user activity timestamp is updated during Mount() and
  // periodically while mounted, other Keyset fields remains the same
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  InsertTestUsers(&kDefaultUsers[9], 1);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_,
              CreateDirectory(AnyOf(
                  MountHelper::GetNewUserPath(user->username),
                  Property(&FilePath::value, StartsWith(kImageDir.value())))))
      .WillRepeatedly(Return(true));

  user->InjectKeyset(&platform_, true);
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  // Mount()
  MountError error;
  ExpectCryptohomeMount(*user);
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      GetDefaultMountArgs(),
                                      /* is_pristine */ false, &error));

  // Update the timestamp. Normally it is called in MountTask::Run() in
  // background but here in the test we must call it manually.
  static const int kMagicTimestamp = 123;
  brillo::Blob updated_keyset;
  std::string timestamp_str;
  EXPECT_CALL(platform_,
              WriteStringToFileAtomicDurable(user->timestamp_path, _, _))
      .WillRepeatedly(DoAll(SaveArg<1>(&timestamp_str), Return(true)));
  EXPECT_CALL(platform_, GetCurrentTime())
      .WillOnce(Return(base::Time::FromInternalValue(kMagicTimestamp)));
  mount_->UpdateCurrentUserActivityTimestamp(0, 0);

  // Check that last activity timestamp is updated.
  ASSERT_TRUE(platform_.FileExists(user->timestamp_path));
  Timestamp tstamp;
  tstamp.ParseFromString(timestamp_str);
  EXPECT_EQ(kMagicTimestamp, tstamp.timestamp());
}

TEST_P(MountTest, RememberMountOrderingTest) {
  // Checks that mounts made with MountAndPush/BindAndPush are undone in the
  // right order.
  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  FilePath src("/src");
  FilePath dest0("/dest/foo");
  FilePath dest1("/dest/bar");
  FilePath dest2("/dest/baz");
  {
    InSequence sequence;
    EXPECT_CALL(platform_, Mount(src, dest0, _, kDefaultMountFlags, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Bind(src, dest1)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Mount(src, dest2, _, kDefaultMountFlags, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest2, _, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest1, _, _)).WillOnce(Return(true));
    EXPECT_CALL(platform_, Unmount(dest0, _, _)).WillOnce(Return(true));

    EXPECT_TRUE(mnt_helper.MountAndPush(src, dest0, "", ""));
    EXPECT_TRUE(mnt_helper.BindAndPush(src, dest1));
    EXPECT_TRUE(mnt_helper.MountAndPush(src, dest2, "", ""));
    mnt_helper.UnmountAll();
  }
}

TEST_P(MountTest, CreateTrackedSubdirectoriesReplaceExistingDir) {
  EXPECT_TRUE(DoMountInit());
  InsertTestUsers(&kDefaultUsers[0], 1);
  TestUser* user = &helper_.users[0];

  FilePath dest_dir;
  if (ShouldTestEcryptfs()) {
    dest_dir = user->vault_path;
    mount_->mount_type_ = ::cryptohome::MountType::ECRYPTFS;
  } else {
    dest_dir = user->vault_mount_path;
    mount_->mount_type_ = ::cryptohome::MountType::DIR_CRYPTO;
  }
  EXPECT_CALL(platform_, DirectoryExists(dest_dir)).WillOnce(Return(true));

  // Expectations for each tracked subdirectory.
  for (const auto& tracked_dir : MountHelper::GetTrackedSubdirectories()) {
    const FilePath tracked_dir_path = dest_dir.Append(tracked_dir);
    const FilePath userside_dir = user->vault_mount_path.Append(tracked_dir);
    // Simulate the case there already exists a non-passthrough-dir
    if (ShouldTestEcryptfs()) {
      // For ecryptfs, delete and replace the existing directory.
      EXPECT_CALL(platform_, DirectoryExists(userside_dir))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DeleteFile(userside_dir, true))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DeleteFile(tracked_dir_path, false))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_, DirectoryExists(tracked_dir_path))
          .WillOnce(Return(false))
          .WillOnce(Return(false));
      EXPECT_CALL(platform_, CreateDirectory(tracked_dir_path))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_,
                  SetOwnership(tracked_dir_path, fake_platform::kChronosUID,
                               fake_platform::kChronosGID, true))
          .WillOnce(Return(true));
    } else {
      // For dircrypto, just skip the directory creation.
      EXPECT_CALL(platform_, DirectoryExists(tracked_dir_path))
          .WillOnce(Return(true));
      EXPECT_CALL(platform_,
                  SetExtendedFileAttribute(
                      tracked_dir_path, kTrackedDirectoryNameAttribute,
                      StrEq(tracked_dir_path.BaseName().value()),
                      tracked_dir_path.BaseName().value().size()))
          .WillOnce(Return(true));
    }
  }
  // Run the method.
  EXPECT_TRUE(mount_->CreateTrackedSubdirectories(user->username));
}

TEST_P(MountTest, MountCryptohomePreviousMigrationIncomplete) {
  // Checks that if both ecryptfs and dircrypto home directories
  // exist, fails with an error.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  // Prepare a placeholder user and a key.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  // Mock the situation that both types of data directory exists.
  EXPECT_CALL(platform_,
              DirectoryExists(AnyOf(user->vault_path, user->vault_mount_path,
                                    user->user_vault_path)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, GetDirCryptoKeyState(user->vault_mount_path))
      .WillRepeatedly(Return(dircrypto::KeyState::ENCRYPTED));

  MountError error = MOUNT_ERROR_NONE;
  ASSERT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                       GetDefaultMountArgs(),
                                       /* is_pristine */ false, &error));
  ASSERT_EQ(MOUNT_ERROR_PREVIOUS_MIGRATION_INCOMPLETE, error);
}

TEST_P(MountTest, MountCryptohomeToMigrateFromEcryptfs) {
  // Checks that to_migrate_from_ecryptfs option is handled correctly.
  // When the existing vault is ecryptfs, mount it to a temporary location while
  // setting up a new dircrypto directory.
  // When the existing vault is dircrypto, just fail.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];

  // Inject dircrypto user paths.
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, false /* is_ecryptfs */);

  if (ShouldTestEcryptfs()) {
    // Inject user ecryptfs paths too.
    user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                          fake_platform::kChronosGID, fake_platform::kSharedGID,
                          kDaemonGid, true /* is_ecryptfs */);

    // When an ecryptfs vault exists, mount it to a temporary location.
    FilePath temporary_mount = user->base_path.Append(kTemporaryMountDir);
    EXPECT_CALL(platform_, CreateDirectory(temporary_mount))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, Mount(user->vault_path, temporary_mount, "ecryptfs",
                                 kDefaultMountFlags, _))
        .WillOnce(Return(true));

    // Key set up for both dircrypto and ecryptfs.
    ExpectCryptohomeKeySetupForDircrypto(*user);
    ExpectCryptohomeKeySetupForEcryptfs(*user);

    EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
        .WillOnce(Return(false));

    EXPECT_CALL(platform_, CreateDirectory(user->vault_mount_path))
        .WillRepeatedly(Return(true));
  }

  EXPECT_CALL(platform_,
              CreateDirectory(MountHelper::GetNewUserPath(user->username)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));

  MountError error = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.to_migrate_from_ecryptfs = true;
  if (ShouldTestEcryptfs()) {
    EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                        mount_args,
                                        /* is_pristine */ false, &error));
  } else {
    // Fail if the existing vault is not ecryptfs.
    EXPECT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                         mount_args,
                                         /* is_pristine */ false, &error));
  }
}

TEST_P(MountTest, MountCryptohomeShadowOnly) {
  // Checks that the shadow_only option is handled correctly.
  InsertTestUsers(&kDefaultUsers[10], 1);
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(DoMountInit());

  TestUser* user = &helper_.users[0];

  // Inject dircrypto user paths.
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  ExpectCryptohomeMountShadowOnly(*user);

  MountError error = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.shadow_only = true;
  EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      mount_args,
                                      /* is_pristine */ false, &error));
}

TEST_P(MountTest, MountCryptohomeForceDircrypto) {
  // Checks that the force-dircrypto flag correctly rejects to mount ecryptfs.
  EXPECT_CALL(platform_, DirectoryExists(kImageDir))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(base::FilePath(kLockedToSingleUserFile)))
      .WillRepeatedly(Return(false));
  EXPECT_TRUE(DoMountInit());

  // Prepare a placeholder user and a key.
  InsertTestUsers(&kDefaultUsers[10], 1);
  TestUser* user = &helper_.users[0];
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());

  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));

  // Mock setup for successful mount when dircrypto is tested.
  if (!ShouldTestEcryptfs()) {
    ExpectCryptohomeMount(*user);

    // Expectations for tracked subdirectories
    EXPECT_CALL(platform_, DirectoryExists(Property(
                               &FilePath::value,
                               StartsWith(user->vault_mount_path.value()))))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_,
                SetExtendedFileAttribute(
                    Property(&FilePath::value,
                             StartsWith(user->vault_mount_path.value())),
                    _, _, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, FileExists(Property(
                               &FilePath::value,
                               StartsWith(user->vault_mount_path.value()))))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(
        platform_,
        SetGroupAccessible(Property(&FilePath::value,
                                    StartsWith(user->vault_mount_path.value())),
                           _, _))
        .WillRepeatedly(Return(true));
  }


  MountError error = MOUNT_ERROR_NONE;
  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.force_dircrypto = true;

  if (ShouldTestEcryptfs()) {
    // Should reject mounting ecryptfs vault.
    EXPECT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                         mount_args,
                                         /* is_pristine */ false, &error));
    EXPECT_EQ(MOUNT_ERROR_OLD_ENCRYPTION, error);
  } else {
    // Should succeed in mounting in dircrypto.
    EXPECT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                        mount_args,
                                        /* is_pristine */ false, &error));
    EXPECT_EQ(MOUNT_ERROR_NONE, error);
  }
}

// Test setup that initially has no cryptohomes.
const TestUserInfo kNoUsers[] = {
    {"user0@invalid.domain", "zero", false},
    {"user1@invalid.domain", "odin", false},
    {"user2@invalid.domain", "dwaa", false},
    {"owner@invalid.domain", "1234", false},
};
const int kNoUserCount = base::size(kNoUsers);

// Test setup that initially has a cryptohome for the owner only.
const TestUserInfo kOwnerOnlyUsers[] = {
    {"user0@invalid.domain", "zero", false},
    {"user1@invalid.domain", "odin", false},
    {"user2@invalid.domain", "dwaa", false},
    {"owner@invalid.domain", "1234", true},
};
const int kOwnerOnlyUserCount = base::size(kOwnerOnlyUsers);

// Test setup that initially has cryptohomes for all users.
const TestUserInfo kAlternateUsers[] = {
    {"user0@invalid.domain", "zero", true},
    {"user1@invalid.domain", "odin", true},
    {"user2@invalid.domain", "dwaa", true},
    {"owner@invalid.domain", "1234", true},
};
const int kAlternateUserCount = base::size(kAlternateUsers);

class AltImageTest : public MountTest {
 public:
  AltImageTest() {}
  ~AltImageTest() { MountTest::TearDown(); }

  void SetUpAltImage(const TestUserInfo* users, int user_count) {
    // Set up fresh users.
    MountTest::SetUp();
    InsertTestUsers(users, user_count);

    EXPECT_CALL(platform_, DirectoryExists(kImageDir))
        .WillRepeatedly(Return(true));
    EXPECT_TRUE(DoMountInit());
  }

  // Set the user with specified |key_file| old.
  bool SetUserTimestamp(TestUser* user, base::Time timestamp) {
    SerializedVaultKeyset serialized;
    if (!LoadSerializedKeyset(user->credentials, &serialized)) {
      LOG(ERROR) << "Failed to parse keyset for " << user->username;
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

  void PrepareHomedirs(bool inject_keyset,
                       const std::vector<int>* delete_vaults,
                       const std::vector<int>* mounted_vaults) {
    bool populate_vaults = (vaults_.size() == 0);
    // const string contents = "some encrypted contents";
    for (int user = 0; user != static_cast<int>(helper_.users.size()); user++) {
      // Let their Cache dirs be filled with some data.
      // Guarded to keep this function reusable.
      if (populate_vaults) {
        EXPECT_CALL(platform_,
                    DirectoryExists(Property(
                        &FilePath::value,
                        StartsWith(helper_.users[user].base_path.value()))))
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

      // After Cache & GCache are depleted. Users are deleted. To do so cleanly,
      // their keysets timestamps are read into an in-memory.
      if (inject_keyset && !mounted_user)
        helper_.users[user].InjectKeyset(&platform_, false);
      if (delete_user) {
        EXPECT_CALL(platform_, DeleteFile(helper_.users[user].base_path, true))
            .WillOnce(Return(true));
      }
    }
  }

  std::vector<FilePath> vaults_;

 private:
  DISALLOW_COPY_AND_ASSIGN(AltImageTest);
};

class EphemeralNoUserSystemTest : public AltImageTest {
 public:
  EphemeralNoUserSystemTest() {}

  void SetUp() { SetUpAltImage(kNoUsers, kNoUserCount); }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralNoUserSystemTest);
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralNoUserSystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralNoUserSystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralNoUserSystemTest, CreateMyFilesDownloads) {
  // Checks that MountHelper::SetUpEphemeralCryptohome creates
  // MyFiles/Downloads.
  const FilePath base_path("/ephemeral_home/");
  const FilePath downloads_path = base_path.Append("Downloads");
  const FilePath myfiles_path = base_path.Append("MyFiles");
  const FilePath myfiles_downloads_path = myfiles_path.Append("Downloads");
  const FilePath gcache_path = base_path.Append("GCache");
  const FilePath gcache_v1_path = base_path.Append("GCache").Append("v1");
  const FilePath gcache_v2_path = base_path.Append("GCache").Append("v2");

  // Expecting Downloads to not exist and then be created.
  EXPECT_CALL(platform_, DirectoryExists(downloads_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(downloads_path, fake_platform::kChronosUID,
                           fake_platform::kChronosGID, _))
      .WillOnce(Return(true));
  // Expecting MyFiles to not exist and then be created.
  EXPECT_CALL(platform_, DirectoryExists(myfiles_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(myfiles_path)).WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(myfiles_path, fake_platform::kChronosUID,
                                      fake_platform::kChronosGID, _))
      .WillOnce(Return(true));
  // Expecting MyFiles/Downloads to not exist and then be created, with right
  // user and group.
  EXPECT_CALL(platform_, DirectoryExists(myfiles_downloads_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(myfiles_downloads_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(myfiles_downloads_path, fake_platform::kChronosUID,
                           fake_platform::kChronosGID, _))
      .WillOnce(Return(true));

  // Expect GCache and Gcache/v2 to be created with the right user and group.
  EXPECT_CALL(platform_, DirectoryExists(gcache_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(gcache_path)).WillOnce(Return(true));
  EXPECT_CALL(platform_, SetOwnership(gcache_path, fake_platform::kChronosUID,
                                      fake_platform::kChronosGID, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(gcache_v2_path))
      .WillOnce(Return(false))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(gcache_v2_path))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetOwnership(gcache_v2_path, fake_platform::kChronosUID,
                           fake_platform::kChronosGID, _))
      .WillOnce(Return(true));

  EXPECT_CALL(platform_, SetOwnership(base_path, fake_platform::kChronosUID,
                                      fake_platform::kSharedGID, _))
      .WillOnce(Return(true));

  // Expectaction for Mount::SetupGroupAccess
  // These files should exist. Then get group accessible called on them.
  EXPECT_CALL(platform_, DirectoryExists(AnyOf(base_path, gcache_v1_path)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              SetGroupAccessible(AnyOf(base_path, myfiles_path, downloads_path,
                                       myfiles_downloads_path, gcache_path,
                                       gcache_v1_path, gcache_v2_path),
                                 fake_platform::kSharedGID, _))
      .WillRepeatedly(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  ASSERT_TRUE(mnt_helper.SetUpEphemeralCryptohome(base_path));
}

TEST_P(EphemeralNoUserSystemTest, CreateMyFilesDownloadsAlreadyExists) {
  // Checks that MountHelper::SetUpEphemeralCryptohome doesn't re-recreate if
  // already exists, just sets the ownership and group access for |base_path|.
  const FilePath base_path("/ephemeral_home/");
  const FilePath downloads_path = base_path.Append("Downloads");
  const FilePath myfiles_path = base_path.Append("MyFiles");
  const FilePath myfiles_downloads_path = myfiles_path.Append("Downloads");
  const auto gcache_dirs = Property(
      &FilePath::value, StartsWith(base_path.Append("GCache").value()));

  EXPECT_CALL(platform_, SetOwnership(base_path, fake_platform::kChronosUID,
                                      fake_platform::kSharedGID, _))
      .WillOnce(Return(true));

  // Expecting Downloads and MyFiles/Downloads to exist thus CreateDirectory
  // isn't called.
  EXPECT_CALL(platform_,
              DirectoryExists(AnyOf(base_path, myfiles_path, downloads_path,
                                    myfiles_downloads_path, gcache_dirs)))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              SetGroupAccessible(AnyOf(base_path, myfiles_path, downloads_path,
                                       myfiles_downloads_path, gcache_dirs),
                                 fake_platform::kSharedGID, _))
      .WillRepeatedly(Return(true));

  MountHelper mnt_helper(fake_platform::kChronosUID, fake_platform::kChronosGID,
                         fake_platform::kSharedGID, kImageDir, kSkelDir,
                         helper_.system_salt, true /*legacy_mount*/,
                         &platform_);

  ASSERT_TRUE(mnt_helper.SetUpEphemeralCryptohome(base_path));
}

TEST_P(EphemeralNoUserSystemTest, OwnerUnknownMountCreateTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a regular vault is created and mounted.
  set_policy(false, "", true);

  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
      .WillOnce(Return(ShouldTestEcryptfs()))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_mount_path))
      .WillRepeatedly(Return(false));
  ExpectCryptohomeKeySetup(*user);
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(user->credentials), Return(true)));
  EXPECT_CALL(platform_, DirectoryExists(Property(
                             &FilePath::value,
                             StartsWith(user->user_vault_mount_path.value()))))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              Mount(_, _, kEphemeralMountType, kDefaultMountFlags, _))
      .Times(0);
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(user->vault_mount_path))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, IsDirectoryMounted(FilePath("/home/chronos/user")))
      .WillOnce(Return(false));
  ExpectDownloadsBindMounts(*user);
  ExpectDaemonStoreMounts(*user, false /* is_ephemeral */);

  // First user to login -> an owner.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::Owner)).WillOnce(Return(true));

  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      mount_args,
                                      /* is_pristine */ true, &error));

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));

  // Unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralNoUserSystemTest, MountSetUserTypeFailTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a regular vault is created and mounted.
  set_policy(false, "", true);

  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_path))
      .WillOnce(Return(ShouldTestEcryptfs()))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(user->vault_mount_path))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, GetFileEnumerator(_, _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));

  if (ShouldTestEcryptfs()) {
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
        .WillRepeatedly(Return(true));
  } else {
    EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, SetDirCryptoKey(user->vault_mount_path, _))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, InvalidateDirCryptoKey(_, kImageDir))
        .WillRepeatedly(Return(true));
  }

  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, WriteFileAtomicDurable(user->keyset_path, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, ReadFile(user->keyset_path, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(user->credentials), Return(true)));
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_vault_path.value()))))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              Mount(_, _, kEphemeralMountType, kDefaultMountFlags, _))
      .Times(0);
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Bind(_, _)).WillRepeatedly(Return(true));

  // Inject the failure.
  EXPECT_CALL(tpm_, SetUserType(_)).WillRepeatedly(Return(false));

  EXPECT_CALL(platform_, GetFileEnumerator(kSkelDir, _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  EXPECT_CALL(
      platform_,
      GetFileEnumerator(
          Property(&FilePath::value, EndsWith("MyFiles/Downloads")), _, _))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()))
      .WillOnce(Return(new NiceMock<MockFileEnumerator>()));

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_FALSE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                       mount_args,
                                       /* is_pristine */ true, &error));
  ASSERT_EQ(MOUNT_ERROR_TPM_COMM_ERROR, error);
}

// TODO(wad) Duplicate these tests with multiple mounts instead of one.

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountNoCreateTest) {
  // Checks that when a device is enterprise enrolled, a tmpfs cryptohome is
  // mounted and no regular vault is created.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  // Enterprise enrolled -> no one is the owner.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  // Implicit unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));
}

TEST_P(EphemeralNoUserSystemTest, OwnerUnknownMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, a mount request with the |ensure_ephemeral| flag set fails.
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);
  EXPECT_CALL(tpm_, SetUserType(_)).Times(0);

  ASSERT_EQ(MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountIsEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |is_ephemeral| flag set causes a tmpfs cryptohome to be mounted and no
  // regular vault to be created.
  set_policy(true, "", false);
  mount_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  ExpectEphemeralCryptohomeMount(*user);

  // Enterprise enrolled -> no one is the owner.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, DetachLoop(kLoopDevice)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Unmount(user->ephemeral_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));

  ExpectDownloadsUnmounts(*user);

  // Unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  EXPECT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountStatVFSFailure) {
  // Checks the case when ephemeral statvfs call fails.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountCreateSparseDirFailure) {
  // Checks the case when directory for ephemeral sparse files fails to be
  // created.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(MountHelper::GetEphemeralSparseFile(
                                             user->obfuscated_username)
                                             .DirName()))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountCreateSparseFailure) {
  // Checks the case when ephemeral sparse file fails to create.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename, _)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountAttachLoopFailure) {
  // Checks that when ephemeral loop device fails to attach, clean up happens
  // appropriately.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename, _)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
      .WillOnce(Return(FilePath()));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountFormatFailure) {
  // Checks that when ephemeral loop device fails to be formatted, clean up
  // happens appropriately.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(0);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename, _)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(false));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, EnterpriseMountEnsureUserMountFailure) {
  // Checks that when ephemeral mount fails to ensure mount points, clean up
  // happens appropriately.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  const TestUser* const user = &helper_.users[0];
  const FilePath ephemeral_filename =
      MountHelper::GetEphemeralSparseFile(user->obfuscated_username);

  EXPECT_CALL(platform_, DetachLoop(_)).Times(1);
  EXPECT_CALL(platform_, DeleteFile(ephemeral_filename, _)).Times(1);
  ExpectCryptohomeRemoval(*user);

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(ephemeral_filename, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              FormatExt4(ephemeral_filename, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(ephemeral_filename))
      .WillOnce(Return(FilePath("/dev/loop7")));
  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(ephemeral_filename.DirName()))
      .WillOnce(Return(true));


  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_FATAL,
            mount_->MountEphemeralCryptohome(user->username));
}

class EphemeralOwnerOnlySystemTest : public AltImageTest {
 public:
  EphemeralOwnerOnlySystemTest() {}

  void SetUp() { SetUpAltImage(kOwnerOnlyUsers, kOwnerOnlyUserCount); }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralOwnerOnlySystemTest);
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralOwnerOnlySystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralOwnerOnlySystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralOwnerOnlySystemTest, MountNoCreateTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a tmpfs cryptohome is mounted and no regular vault is created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, true);

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(owner_only), Return(true)));

  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));

  ExpectEphemeralCryptohomeMount(*user);

  // Different user -> not an owner.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(user->ephemeral_mount_path, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));

  ExpectDownloadsUnmounts(*user);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  // Unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralOwnerOnlySystemTest, NonOwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |is_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted and no regular vault to be created.
  TestUser* owner = &helper_.users[3];
  TestUser* user = &helper_.users[0];
  set_policy(true, owner->username, false);

  // Always removes non-owner cryptohomes.
  std::vector<FilePath> owner_only;
  owner_only.push_back(owner->base_path);

  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(owner_only), Return(true)));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  // Different user -> not an owner.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  // Implicit unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));
}

TEST_P(EphemeralOwnerOnlySystemTest, OwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for the owner with the |ensure_ephemeral| flag set fails.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);

  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);
  EXPECT_CALL(tpm_, SetUserType(_)).Times(0);

  ASSERT_EQ(MOUNT_ERROR_EPHEMERAL_MOUNT_BY_OWNER,
            mount_->MountEphemeralCryptohome(owner->username));
}

class EphemeralExistingUserSystemTest : public AltImageTest {
 public:
  EphemeralExistingUserSystemTest() {}

  void SetUp() { SetUpAltImage(kAlternateUsers, kAlternateUserCount); }

 private:
  DISALLOW_COPY_AND_ASSIGN(EphemeralExistingUserSystemTest);
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         EphemeralExistingUserSystemTest,
                         ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(WithDircrypto,
                         EphemeralExistingUserSystemTest,
                         ::testing::Values(false));

TEST_P(EphemeralExistingUserSystemTest, OwnerUnknownMountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while mounting.
  set_policy(false, "", true);
  TestUser* user = &helper_.users[0];

  // No c-homes will be removed.  The rest of the mocking just gets us to
  // Mount().
  for (auto& user : helper_.users)
    user.InjectUserPaths(&platform_, fake_platform::kChronosUID,
                         fake_platform::kChronosGID, fake_platform::kSharedGID,
                         kDaemonGid, ShouldTestEcryptfs());

  std::vector<FilePath> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(_, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, Stat(_, _)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, CreateDirectory(user->vault_path)).Times(0);
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));

  ExpectCryptohomeMount(*user);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));

  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, DeleteFile(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(platform_,
              Mount(_, _, kEphemeralMountType, kDefaultMountFlags, _))
      .Times(0);

  Mount::MountArgs mount_args = GetDefaultMountArgs();
  mount_args.create_if_missing = true;
  MountError error;
  ASSERT_TRUE(mount_->MountCryptohome(user->username, FileSystemKeys(),
                                      mount_args,
                                      /* is_pristine */ false, &error));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  if (ShouldTestEcryptfs()) {
    EXPECT_CALL(platform_,
                Unmount(Property(&FilePath::value, EndsWith("/mount")), _, _))
        .WillOnce(Return(true));  // user mount
  }
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, Unmount(Property(&FilePath::value,
                                          StartsWith(kRunDaemonStoreBaseDir)),
                                 _, _))
      .WillOnce(Return(true));  // daemon store mounts
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user);
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseMountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while mounting.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);
  TestUser* user = &helper_.users[0];

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<FilePath> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillOnce(DoAll(SetArgPointee<2>(vaults_), Return(true)))
      // Don't re-delete on Unmount.
      .WillRepeatedly(DoAll(SetArgPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      DeleteFile(MountHelper::GetEphemeralSparseFile(user->obfuscated_username),
                 _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              EnumerateDirectoryEntries(user->ephemeral_mount_path, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_, DeleteFile(user->root_ephemeral_mount_path, true))
      .WillOnce(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  // Deleting users will cause each user's shadow root subdir to be
  // searched for LE credentials.
  for (const auto& user : helper_.users) {
    EXPECT_CALL(
        platform_,
        GetFileEnumerator(kImageDir.Append(user.obfuscated_username), false, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  }

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, DeleteFile(user->ephemeral_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user);
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, MountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all non-owner cryptohomes are removed while mounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);
  TestUser* user = &helper_.users[0];

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);  // Mounting user shouldn't use be persistent.
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  // Expect all users but the owner to be removed.
  PrepareHomedirs(true, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  std::vector<FilePath> no_vaults;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillOnce(DoAll(SetArgPointee<2>(vaults_), Return(true)))
      // Don't re-delete on Unmount.
      .WillRepeatedly(DoAll(SetArgPointee<2>(no_vaults), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));
  user->InjectUserPaths(&platform_, fake_platform::kChronosUID,
                        fake_platform::kChronosGID, fake_platform::kSharedGID,
                        kDaemonGid, ShouldTestEcryptfs());
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      DeleteFile(MountHelper::GetEphemeralSparseFile(user->obfuscated_username),
                 _))
      .WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              EnumerateDirectoryEntries(user->ephemeral_mount_path, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_, DeleteFile(user->root_ephemeral_mount_path, true))
      .WillOnce(Return(true));

  ExpectEphemeralCryptohomeMount(*user);

  // Deleting users will cause "going-to-be-deleted" users' shadow root
  // subdir to be searched for LE credentials.
  for (int i = 0; i < helper_.users.size() - 1; i++) {
    TestUser* cur_user = &helper_.users[i];
    EXPECT_CALL(platform_,
                GetFileEnumerator(
                    kImageDir.Append(cur_user->obfuscated_username), false, _))
        .WillOnce(Return(new NiceMock<MockFileEnumerator>()));
  }

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/chronos/u-")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/user/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value, StartsWith("/home/root/")), _, _))
      .WillOnce(Return(true));  // user mount
  EXPECT_CALL(platform_, Unmount(FilePath("/home/chronos/user"), _, _))
      .WillOnce(Return(true));  // legacy mount
  EXPECT_CALL(platform_, DeleteFile(user->ephemeral_mount_path, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  ExpectDownloadsUnmounts(*user);
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, OwnerUnknownUnmountNoRemoveTest) {
  // Checks that when a device is not enterprise enrolled and does not have a
  // known owner, no stale cryptohomes are removed while unmounting.
  set_policy(false, "", true);
  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseUnmountRemoveTest) {
  // Checks that when a device is enterprise enrolled, all stale cryptohomes are
  // removed while unmounting.
  set_policy(false, "", true);
  mount_->set_enterprise_owned(true);

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  expect_deletion.push_back(3);
  PrepareHomedirs(false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, UnmountRemoveTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // all stale cryptohomes are removed while unmounting.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, true);

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  // All users but the owner.
  std::vector<int> expect_deletion;
  expect_deletion.push_back(0);
  expect_deletion.push_back(1);
  expect_deletion.push_back(2);
  PrepareHomedirs(false, &expect_deletion, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));

  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));

  ASSERT_TRUE(mount_->UnmountCryptohome());
}

TEST_P(EphemeralExistingUserSystemTest, NonOwnerMountIsEphemeralTest) {
  // Checks that when a device is not enterprise enrolled and has a known owner,
  // a mount request for a non-owner user with the |is_ephemeral| flag set
  // causes a tmpfs cryptohome to be mounted, even if a regular vault exists for
  // the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  TestUser* owner = &helper_.users[3];
  set_policy(true, owner->username, false);
  TestUser* user = &helper_.users[0];

  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));

  PrepareHomedirs(true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(Property(&FilePath::value,
                                             StartsWith("/home/chronos/user"))))
      .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              EnumerateDirectoryEntries(user->ephemeral_mount_path, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralExistingUserSystemTest, EnterpriseMountIsEphemeralTest) {
  // Checks that when a device is enterprise enrolled, a mount request with the
  // |is_ephemeral| flag set causes a tmpfs cryptohome to be mounted, even
  // if a regular vault exists for the user.
  // Since ephemeral users aren't enabled, no vaults will be deleted.
  set_policy(true, "", false);
  mount_->set_enterprise_owned(true);

  TestUser* user = &helper_.users[0];

  // Mounting user vault won't be deleted, but tmpfs mount should still be
  // used.
  PrepareHomedirs(true, NULL, NULL);

  // Let Mount know how many vaults there are.
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(kImageDir, false, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(vaults_), Return(true)));
  // Don't say any cryptohomes are mounted.
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          AnyOf(FilePath("/home/root/"), FilePath("/home/user/")), _, _))
      .WillRepeatedly(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home/chronos"),
                         MountHelper::GetNewUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(platform_,
              Stat(AnyOf(FilePath("/home"), FilePath("/home/root"),
                         brillo::cryptohome::home::GetRootPath(user->username),
                         FilePath("/home/user"),
                         brillo::cryptohome::home::GetUserPath(user->username)),
                   _))
      .WillRepeatedly(Return(false));
  // Only expect the mounted user to "exist".
  EXPECT_CALL(platform_,
              DirectoryExists(Property(
                  &FilePath::value, StartsWith(user->user_mount_path.value()))))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetPermissions(_, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(Property(&FilePath::value,
                                             StartsWith("/home/chronos/user"))))
      .WillRepeatedly(Return(true));

  helper_.InjectEphemeralSkeleton(&platform_,
                                  FilePath(user->user_ephemeral_mount_path));

  EXPECT_CALL(platform_, Stat(user->root_ephemeral_mount_path, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_,
              EnumerateDirectoryEntries(user->ephemeral_mount_path, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));

  EXPECT_CALL(platform_, Unmount(_, _, _)).WillRepeatedly(Return(true));
  ExpectEphemeralCryptohomeMount(*user);

  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_EQ(MOUNT_ERROR_NONE, mount_->MountEphemeralCryptohome(user->username));
}

TEST_P(EphemeralNoUserSystemTest, MountGuestUserDir) {
  base::stat_wrapper_t fake_root_st;
  fake_root_st.st_uid = 0;
  fake_root_st.st_gid = 0;
  fake_root_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home"), _))
      .Times(3)
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_, Stat(FilePath("/home/root"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_,
              Stat(Property(&FilePath::value, StartsWith("/home/root/")), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, Stat(FilePath("/home/user"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_,
              Stat(Property(&FilePath::value, StartsWith("/home/user/")), _))
      .WillOnce(Return(false));
  base::stat_wrapper_t fake_user_st;
  fake_user_st.st_uid = fake_platform::kChronosUID;
  fake_user_st.st_gid = fake_platform::kChronosGID;
  fake_user_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home/chronos"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_user_st), Return(true)));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillOnce(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));

  EXPECT_CALL(platform_, StatVFS(FilePath(kEphemeralCryptohomeDir), _))
      .WillOnce(Return(true));
  const std::string sparse_prefix =
      FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir).value();
  EXPECT_CALL(platform_,
              CreateSparseFile(
                  Property(&FilePath::value, StartsWith(sparse_prefix)), _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              AttachLoop(Property(&FilePath::value, StartsWith(sparse_prefix))))
      .WillOnce(Return(FilePath("/dev/loop7")));
  EXPECT_CALL(platform_,
              FormatExt4(Property(&FilePath::value, StartsWith(sparse_prefix)),
                         kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Stat(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)), _))
      .WillOnce(Return(false));
  std::vector<FilePath> empty;
  EXPECT_CALL(platform_, EnumerateDirectoryEntries(
                             Property(&FilePath::value,
                                      StartsWith(kEphemeralCryptohomeDir)),
                             _, _))
      .WillOnce(DoAll(SetArgPointee<2>(empty), Return(true)));
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _)).Times(0);
  EXPECT_CALL(platform_, Mount(FilePath("/dev/loop7"), _, kEphemeralMountType,
                               kDefaultMountFlags, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_,
              SetSELinuxContext(Property(&FilePath::value,
                                         StartsWith(kEphemeralCryptohomeDir)),
                                cryptohome::kEphemeralCryptohomeRootContext))
      .WillOnce(Return(true));

  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/root/"))))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/user/"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(Property(&FilePath::value,
                                       StartsWith(kEphemeralCryptohomeDir)),
                              FilePath("/home/chronos/user")))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)),
           Property(&FilePath::value, StartsWith("/home/chronos/u-"))))
      .WillOnce(Return(true));
  // Binding Downloads to MyFiles/Downloads.
  EXPECT_CALL(platform_,
              Bind(Property(&FilePath::value, StartsWith("/home/chronos/u-")),
                   Property(&FilePath::value, StartsWith("/home/chronos/u-"))))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Bind(Property(&FilePath::value, StartsWith("/home/chronos/user/")),
           Property(&FilePath::value, StartsWith("/home/chronos/user/"))))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(
                             FilePath("/home/chronos/user/MyFiles/Downloads")))
      .WillOnce(Return(false));  // first mount
  EXPECT_CALL(platform_,
              Bind(Property(&FilePath::value, StartsWith("/home/user/")),
                   Property(&FilePath::value, StartsWith("/home/user/"))))
      .WillOnce(Return(true));

  // Guest -> not an owner.
  // Also will be called on implicit Unmount.
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));

  ASSERT_TRUE(mount_->MountGuestCryptohome());

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  // Implicit unmount triggers setting user type to non-owner.
  testing::Mock::VerifyAndClearExpectations(&tpm_);
  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(true));
}

TEST_P(EphemeralNoUserSystemTest, MountGuestUserFailSetUserType) {
  base::stat_wrapper_t fake_root_st;
  fake_root_st.st_uid = 0;
  fake_root_st.st_gid = 0;
  fake_root_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home"), _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_, Stat(FilePath("/home/root"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_,
              Stat(Property(&FilePath::value, StartsWith("/home/root/")), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, Stat(FilePath("/home/user"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_root_st), Return(true)));
  EXPECT_CALL(platform_,
              Stat(Property(&FilePath::value, StartsWith("/home/user/")), _))
      .WillOnce(Return(false));
  base::stat_wrapper_t fake_user_st;
  fake_user_st.st_uid = fake_platform::kChronosUID;
  fake_user_st.st_gid = fake_platform::kChronosGID;
  fake_user_st.st_mode = S_IFDIR | S_IRWXU;
  EXPECT_CALL(platform_, Stat(FilePath("/home/chronos"), _))
      .WillOnce(DoAll(SetArgPointee<1>(fake_user_st), Return(true)));
  EXPECT_CALL(platform_, CreateDirectory(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetOwnership(_, _, _, _)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, SetGroupAccessible(_, _, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, IsDirectoryMounted(_)).WillRepeatedly(Return(false));
  EXPECT_CALL(platform_, DirectoryExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, FileExists(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, StatVFS(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, CreateSparseFile(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, AttachLoop(_))
      .WillOnce(Return(FilePath("/dev/loop7")));
  EXPECT_CALL(platform_, FormatExt4(_, kDefaultExt4FormatOpts, 0))
      .WillOnce(Return(true));
  EXPECT_CALL(
      platform_,
      Stat(Property(&FilePath::value, StartsWith(kEphemeralCryptohomeDir)), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, Mount(_, _, _, kDefaultMountFlags, _))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_,
              SetSELinuxContext(Property(&FilePath::value,
                                         StartsWith(kEphemeralCryptohomeDir)),
                                cryptohome::kEphemeralCryptohomeRootContext))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _)).WillRepeatedly(Return(true));

  EXPECT_CALL(tpm_, SetUserType(Tpm::UserType::NonOwner))
      .WillOnce(Return(false));

  // Unmount succeeds.
  ON_CALL(platform_, Unmount(_, _, _)).WillByDefault(Return(true));
  // Detach succeeds.
  ON_CALL(platform_, DetachLoop(_)).WillByDefault(Return(true));

  ASSERT_FALSE(mount_->MountGuestCryptohome());
}

}  // namespace cryptohome
