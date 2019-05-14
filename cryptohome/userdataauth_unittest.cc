// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/userdataauth.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <chaps/token_manager_client_mock.h>
#include <vector>

#include "cryptohome/mock_crypto.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_install_attributes.h"
#include "cryptohome/mock_mount.h"
#include "cryptohome/mock_mount_factory.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_tpm.h"
#include "cryptohome/mock_tpm_init.h"
#include "cryptohome/mock_vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;

using ::testing::_;
using ::testing::AtLeast;
using ::testing::EndsWith;
using ::testing::Invoke;
using ::testing::Mock;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::WithArgs;

namespace cryptohome {

namespace {

bool AssignSalt(size_t size, SecureBlob* salt) {
  SecureBlob fake_salt(size, 'S');
  salt->swap(fake_salt);
  return true;
}

}  // namespace

// UserDataAuthTestNotInitialized is a test fixture that does not call
// UserDataAuth::Initialize() during setup. Therefore, it's suited to tests that
// can be conducted without calling UserDataAuth::Initialize(), or for tests
// that wants some flexibility before calling UserDataAuth::Initialize(), note
// that in this case the test have to call UserDataAuth::Initialize().
class UserDataAuthTestNotInitialized : public ::testing::Test {
 public:
  UserDataAuthTestNotInitialized() = default;
  ~UserDataAuthTestNotInitialized() override = default;

  void SetUp() override {
    tpm_init_.set_tpm(&tpm_);

    userdataauth_.set_crypto(&crypto_);
    userdataauth_.set_homedirs(&homedirs_);
    userdataauth_.set_install_attrs(&attrs_);
    userdataauth_.set_tpm(&tpm_);
    userdataauth_.set_tpm_init(&tpm_init_);
    userdataauth_.set_platform(&platform_);
    userdataauth_.set_chaps_client(&chaps_client_);
    userdataauth_.set_disable_threading(true);
    homedirs_.set_crypto(&crypto_);
    homedirs_.set_platform(&platform_);
    ON_CALL(homedirs_, Init(_, _, _)).WillByDefault(Return(true));
    // Empty token list by default.  The effect is that there are no attempts
    // to unload tokens unless a test explicitly sets up the token list.
    ON_CALL(chaps_client_, GetTokenList(_, _)).WillByDefault(Return(true));
    // Skip CleanUpStaleMounts by default.
    ON_CALL(platform_, GetMountsBySourcePrefix(_, _))
        .WillByDefault(Return(false));
    // Setup fake salt by default.
    ON_CALL(crypto_, GetOrCreateSalt(_, _, _, _))
        .WillByDefault(WithArgs<1, 3>(Invoke(AssignSalt)));
  }

  // This is a utility function for tests to setup a mount for a particular
  // user. After calling this function, |mount_| is available for use.
  void SetupMount(const std::string& username) {
    mount_ = new NiceMock<MockMount>();
    userdataauth_.set_mount_for_user(username, mount_.get());
  }

 protected:
  // Mock Crypto object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockCrypto> crypto_;

  // Mock HomeDirs object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockHomeDirs> homedirs_;

  // Mock InstallAttributes object, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<MockInstallAttributes> attrs_;

  // Mock Platform object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockPlatform> platform_;

  // Mock TPM object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockTpm> tpm_;

  // Mock TPM Init object, will be passed to UserDataAuth for its internal use.
  NiceMock<MockTpmInit> tpm_init_;

  // Mock chaps token manager client, will be passed to UserDataAuth for its
  // internal use.
  NiceMock<chaps::TokenManagerClientMock> chaps_client_;

  // This is used to hold the mount object when we create a mock mount with
  // SetupMount().
  scoped_refptr<MockMount> mount_;

  // Declare |userdataauth_| last so it gets destroyed before all the mocks.
  // This is important because otherwise the background thread may call into
  // mocks that have already been destroyed.
  UserDataAuth userdataauth_;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthTestNotInitialized);
};

// Standard, fully initialized UserDataAuth test fixture
class UserDataAuthTest : public UserDataAuthTestNotInitialized {
 public:
  UserDataAuthTest() = default;
  ~UserDataAuthTest() override = default;

  void SetUp() override {
    UserDataAuthTestNotInitialized::SetUp();
    ASSERT_TRUE(userdataauth_.Initialize());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthTest);
};

namespace CryptohomeErrorCodeEquivalenceTest {
// This test is completely static, so it is not wrapped in the TEST_F() block.
static_assert(static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_NOT_SET) ==
                  static_cast<int>(cryptohome::CRYPTOHOME_ERROR_NOT_SET),
              "Enum member CRYPTOHOME_ERROR_NOT_SET differs between "
              "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_ACCOUNT_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_NOT_IMPLEMENTED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_NOT_IMPLEMENTED),
    "Enum member CRYPTOHOME_ERROR_NOT_IMPLEMENTED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_FATAL) ==
                  static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_FATAL),
              "Enum member CRYPTOHOME_ERROR_MOUNT_FATAL differs between "
              "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY),
    "Enum member CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_COMM_ERROR) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_COMM_ERROR),
    "Enum member CRYPTOHOME_ERROR_TPM_COMM_ERROR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_DEFEND_LOCK),
    "Enum member CRYPTOHOME_ERROR_TPM_DEFEND_LOCK differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT),
    "Enum member CRYPTOHOME_ERROR_TPM_NEEDS_REBOOT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED),
    "Enum member CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED),
    "Enum member CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_LABEL_EXISTS) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_LABEL_EXISTS),
    "Enum member CRYPTOHOME_ERROR_KEY_LABEL_EXISTS differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_BACKING_STORE_FAILURE),
    "Enum member CRYPTOHOME_ERROR_BACKING_STORE_FAILURE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID),
    "Enum member CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_KEY_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_KEY_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_KEY_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(static_cast<int>(
                  user_data_auth::CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID) ==
                  static_cast<int>(
                      cryptohome::CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID),
              "Enum member CRYPTOHOME_ERROR_LOCKBOX_SIGNATURE_INVALID differs "
              "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN),
    "Enum member CRYPTOHOME_ERROR_LOCKBOX_CANNOT_SIGN differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND),
    "Enum member CRYPTOHOME_ERROR_BOOT_ATTRIBUTE_NOT_FOUND differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN),
    "Enum member CRYPTOHOME_ERROR_BOOT_ATTRIBUTES_CANNOT_SIGN differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE),
    "Enum member CRYPTOHOME_ERROR_TPM_EK_NOT_AVAILABLE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_ATTESTATION_NOT_READY) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_ATTESTATION_NOT_READY),
    "Enum member CRYPTOHOME_ERROR_ATTESTATION_NOT_READY differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA),
    "Enum member CRYPTOHOME_ERROR_CANNOT_CONNECT_TO_CA differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT),
    "Enum member CRYPTOHOME_ERROR_CA_REFUSED_ENROLLMENT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE),
    "Enum member CRYPTOHOME_ERROR_CA_REFUSED_CERTIFICATE differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR),
    "Enum member CRYPTOHOME_ERROR_INTERNAL_ATTESTATION_ERROR differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_INVALID "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_STORE "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::
            CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE) ==
        static_cast<int>(
            cryptohome::
                CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE),
    "Enum member CRYPTOHOME_ERROR_FIRMWARE_MANAGEMENT_PARAMETERS_CANNOT_REMOVE "
    "differs between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION),
    "Enum member CRYPTOHOME_ERROR_MOUNT_OLD_ENCRYPTION differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(
        user_data_auth::CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE) ==
        static_cast<int>(
            cryptohome::CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE),
    "Enum member CRYPTOHOME_ERROR_MOUNT_PREVIOUS_MIGRATION_INCOMPLETE differs "
    "between user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED),
    "Enum member CRYPTOHOME_ERROR_MIGRATE_KEY_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_REMOVE_FAILED) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_REMOVE_FAILED),
    "Enum member CRYPTOHOME_ERROR_REMOVE_FAILED differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    static_cast<int>(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT) ==
        static_cast<int>(cryptohome::CRYPTOHOME_ERROR_INVALID_ARGUMENT),
    "Enum member CRYPTOHOME_ERROR_INVALID_ARGUMENT differs between "
    "user_data_auth:: and cryptohome::");
static_assert(
    user_data_auth::CryptohomeErrorCode_MAX == 33,
    "user_data_auth::CrytpohomeErrorCode's element count is incorrect");
static_assert(cryptohome::CryptohomeErrorCode_MAX == 33,
              "cryptohome::CrytpohomeErrorCode's element count is incorrect");
}  // namespace CryptohomeErrorCodeEquivalenceTest

TEST_F(UserDataAuthTest, IsMounted) {
  // By default there are no mount right after initialization
  EXPECT_FALSE(userdataauth_.IsMounted());
  EXPECT_FALSE(userdataauth_.IsMounted("foo@gmail.com"));

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Test the code path that doesn't specify a user, and when there's a mount
  // that's unmounted.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(false));
  EXPECT_FALSE(userdataauth_.IsMounted());

  // Test to see if is_ephemeral works and test the code path that doesn't
  // specify a user.
  bool is_ephemeral = true;
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(true));
  EXPECT_TRUE(userdataauth_.IsMounted("", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);

  // Test to see if is_ephemeral works, and test the code path that specify the
  // user.
  EXPECT_CALL(*mount_, IsMounted()).WillOnce(Return(true));
  EXPECT_CALL(*mount_, IsNonEphemeralMounted()).WillOnce(Return(false));
  EXPECT_TRUE(userdataauth_.IsMounted("foo@gmail.com", &is_ephemeral));
  EXPECT_TRUE(is_ephemeral);

  // Note: IsMounted will not be called in this case.
  EXPECT_FALSE(userdataauth_.IsMounted("bar@gmail.com", &is_ephemeral));
  EXPECT_FALSE(is_ephemeral);
}

TEST_F(UserDataAuthTest, Unmount) {
  // Unmount sanity test.
  // The tests on whether stale mount are cleaned up is in another set of tests
  // called CleanUpStale_*

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // Unmount will be successful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(true));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Unmount should be successful.
  EXPECT_TRUE(userdataauth_.Unmount());

  // It should be unmounted in the end.
  EXPECT_FALSE(userdataauth_.IsMounted());

  // Add another mount associated with bar@gmail.com
  SetupMount("bar@gmail.com");

  // Unmount will be unsuccessful.
  EXPECT_CALL(*mount_, UnmountCryptohome()).WillOnce(Return(false));
  // If anyone asks, this mount is still mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // Unmount should be honest about failures.
  EXPECT_FALSE(userdataauth_.Unmount());

  // Unmount will remove all mounts even if it failed.
  EXPECT_FALSE(userdataauth_.IsMounted());
}

TEST_F(UserDataAuthTest, InitializePkcs11Success) {
  // This test the most common success case for PKCS#11 initialization.

  EXPECT_FALSE(userdataauth_.IsMounted());

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));
  // |mount_| should get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).WillOnce(Return(true));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsInitialized);
}

TEST_F(UserDataAuthTest, InitializePkcs11TpmNotOwned) {
  // Test when TPM isn't owned.

  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  // PKCS#11 will initialization works only when it's mounted.
  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(true));

  // |mount_| should not get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).Times(0);

  // TPM is enabled but not owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(false));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsWaitingOnTPM);

  // We'll need to call InsertPkcs11Token() and IsEnabled() later in the test.
  Mock::VerifyAndClearExpectations(mount_.get());
  Mock::VerifyAndClearExpectations(&tpm_);

  // Next check when the TPM is now owned.

  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(true));

  // |mount_| should get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).WillOnce(Return(true));

  // TPM is enabled and owned.
  ON_CALL(tpm_, IsEnabled()).WillByDefault(Return(true));
  EXPECT_CALL(tpm_, IsOwned()).Times(AtLeast(1)).WillRepeatedly(Return(true));

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kIsInitialized);
}

TEST_F(UserDataAuthTest, InitializePkcs11Unmounted) {
  // Add a mount associated with foo@gmail.com
  SetupMount("foo@gmail.com");

  ON_CALL(*mount_, IsMounted()).WillByDefault(Return(false));
  // The initialization code should at least check, right?
  EXPECT_CALL(*mount_, IsMounted())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(false));

  // |mount_| should not get a request to insert PKCS#11 token.
  EXPECT_CALL(*mount_, InsertPkcs11Token()).Times(0);

  userdataauth_.InitializePkcs11(mount_.get());

  EXPECT_EQ(mount_->pkcs11_state(), cryptohome::Mount::kUninitialized);
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesEnterpriseOwned) {
  EXPECT_CALL(attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "true";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));
  userdataauth_.Initialize();

  EXPECT_TRUE(userdataauth_.IsEnterpriseOwned());
}

TEST_F(UserDataAuthTestNotInitialized, InstallAttributesNotEnterpriseOwned) {
  EXPECT_CALL(attrs_, Init(_)).WillOnce(Return(true));

  std::string str_true = "false";
  std::vector<uint8_t> blob_true(str_true.begin(), str_true.end());
  blob_true.push_back(0);

  EXPECT_CALL(attrs_, Get("enterprise.owned", _))
      .WillOnce(DoAll(SetArgPointee<1>(blob_true), Return(true)));
  userdataauth_.Initialize();

  EXPECT_FALSE(userdataauth_.IsEnterpriseOwned());
}

// ======================= CleanUpStaleMounts tests ==========================

namespace {

struct Mounts {
  const FilePath src;
  const FilePath dst;
};

const Mounts kShadowMounts[] = {
    {FilePath("/home/.shadow/a"), FilePath("/home/user/0")},
    {FilePath("/home/.shadow/a"), FilePath("/home/root/0")},
    {FilePath("/home/.shadow/b"), FilePath("/home/user/1")},
    {FilePath("/home/.shadow/a"), FilePath("/home/chronos/user")},
    {FilePath("/home/.shadow/b"), FilePath("/home/root/1")},
    {FilePath("/home/user/b/Downloads"),
     FilePath("/home/user/b/MyFiles/Downloads")},
    {FilePath("/home/chronos/u-b/Downloads"),
     FilePath("/home/chronos/u-b/MyFiles/Downloads")},
    {FilePath("/home/chronos/user/Downloads"),
     FilePath("/home/chronos/user/MyFiles/Downloads")},
};

const int kShadowMountsCount = 8;

const Mounts kLoopDevMounts[] = {
    {FilePath("/dev/loop7"), FilePath("/run/cryptohome/ephemeral_mount/1")},
    {FilePath("/dev/loop7"), FilePath("/home/user/0")},
    {FilePath("/dev/loop7"), FilePath("/home/root/0")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/u-1")},
    {FilePath("/dev/loop7"), FilePath("/home/chronos/user")},
    {FilePath("/dev/loop1"), FilePath("/opt/google/containers")},
    {FilePath("/dev/loop2"), FilePath("/home/root/1")},
    {FilePath("/dev/loop2"), FilePath("/home/user/1")},
};

// 5 Mounts in the above are from /dev/loop7, which is ephemeral as seen
// in kLoopDevices.
const int kEphemeralMountsCount = 5;

// Constants used by CleanUpStaleMounts tests.
const Platform::LoopDevice kLoopDevices[] = {
    {FilePath("/mnt/stateful_partition/encrypted.block"),
     FilePath("/dev/loop0")},
    {FilePath("/run/cryptohome/ephemeral_data/1"), FilePath("/dev/loop7")},
};

const FilePath kSparseFiles[] = {
    FilePath("/run/cryptohome/ephemeral_data/2"),
    FilePath("/run/cryptohome/ephemeral_data/1"),
};

// Utility functions used by CleanUpStaleMounts tests.
bool StaleShadowMounts(const FilePath& from_prefix,
                       std::multimap<const FilePath, const FilePath>* mounts) {
  if (from_prefix.value() == "/home/.shadow") {
    if (!mounts)
      return true;
    const struct Mounts* m = &kShadowMounts[0];
    for (int i = 0; i < kShadowMountsCount; ++i, ++m) {
      mounts->insert(std::pair<const FilePath, const FilePath>(m->src, m->dst));
    }
    return true;
  }
  return false;
}

bool LoopDeviceMounts(std::multimap<const FilePath, const FilePath>* mounts) {
  if (!mounts)
    return false;
  const Mounts* m = kLoopDevMounts;
  for (int i = 0; i < arraysize(kLoopDevMounts); ++i, ++m)
    mounts->insert(std::make_pair(m->src, m->dst));
  return true;
}

bool EnumerateSparseFiles(const base::FilePath& path,
                          bool is_recursive,
                          std::vector<base::FilePath>* ent_list) {
  if (path != FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir))
    return false;
  ent_list->insert(ent_list->end(), std::begin(kSparseFiles),
                   std::end(kSparseFiles));
  return true;
}

}  // namespace

TEST_F(UserDataAuthTest, CleanUpStale_NoOpenFiles_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted, loop device is
  // detached and sparse file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, everything is kept.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kEphemeralMountsCount - 1);
  std::vector<ProcessInformation> processes(1);
  processes[0].set_process_id(1);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));

  EXPECT_CALL(platform_, Unmount(_, _, _)).Times(0);
  EXPECT_TRUE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_OpenLegacy_Ephemeral_Forced) {
  // Check that when we have ephemeral mounts, no active mounts,
  // and some open filehandles to the legacy homedir, but cleanup is forced,
  // all mounts are unmounted, loop device is detached and file is deleted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(homedirs_.shadow_root(), _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>(
          std::begin(kLoopDevices), std::end(kLoopDevices))));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_))
      .WillOnce(Invoke(LoopDeviceMounts));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Invoke(EnumerateSparseFiles));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _)).Times(0);

  for (int i = 0; i < kEphemeralMountsCount; ++i) {
    EXPECT_CALL(platform_, Unmount(kLoopDevMounts[i].dst, true, _))
        .WillRepeatedly(Return(true));
  }
  EXPECT_CALL(platform_, DetachLoop(FilePath("/dev/loop7")))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[0], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kSparseFiles[1], _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, DeleteFile(kLoopDevMounts[0].dst, _))
      .WillOnce(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(true));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_NoOpenFiles_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and no open filehandles, all stale mounts are unmounted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMountsCount);
  EXPECT_CALL(platform_, Unmount(_, true, _))
      .Times(kShadowMountsCount)
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(userdataauth_.CleanUpStaleMounts(false));
}

TEST_F(UserDataAuthTest, CleanUpStale_EmptyMap_OpenLegacy_ShadowOnly) {
  // Check that when we have a bunch of stale shadow mounts, no active mounts,
  // and some open filehandles to the legacy homedir, all mounts without
  // filehandles are unmounted.

  EXPECT_CALL(platform_, GetMountsBySourcePrefix(_, _))
      .WillOnce(Invoke(StaleShadowMounts));
  EXPECT_CALL(platform_, GetAttachedLoopDevices())
      .WillRepeatedly(Return(std::vector<Platform::LoopDevice>()));
  EXPECT_CALL(platform_, GetLoopDeviceMounts(_)).WillOnce(Return(false));
  EXPECT_CALL(
      platform_,
      EnumerateDirectoryEntries(
          FilePath(kEphemeralCryptohomeDir).Append(kSparseFileDir), _, _))
      .WillOnce(Return(false));
  std::vector<ProcessInformation> processes(1);
  processes[0].set_process_id(1);
  EXPECT_CALL(platform_, GetProcessesWithOpenFiles(_, _))
      .Times(kShadowMountsCount - 1);
  EXPECT_CALL(platform_,
              GetProcessesWithOpenFiles(FilePath("/home/chronos/user"), _))
      .Times(1)
      .WillRepeatedly(SetArgPointee<1>(processes));
  EXPECT_CALL(
      platform_,
      Unmount(Property(&FilePath::value,
                       AnyOf(EndsWith("/1"), EndsWith("/MyFiles/Downloads"))),
              true, _))
      .Times(5)
      .WillRepeatedly(Return(true));
  EXPECT_TRUE(userdataauth_.CleanUpStaleMounts(false));
}

// ==================== Mount and Keys related tests =======================

// A test fixture with some utility functions for testing mount and keys related
// functionalities.
class UserDataAuthExTest : public UserDataAuthTest {
 public:
  UserDataAuthExTest() = default;
  ~UserDataAuthExTest() override = default;

  VaultKeyset* GetNiceMockVaultKeyset(const std::string& obfuscated_username,
                                      const std::string& key_label) const {
    // Note that technically speaking this is not strictly a mock, and probably
    // closer to a stub. However, the underlying class is
    // NiceMock<MockVaultKeyset>, thus we name the method accordingly.
    std::unique_ptr<VaultKeyset> mvk(new NiceMock<MockVaultKeyset>);
    mvk->mutable_serialized()->mutable_key_data()->set_label(key_label);
    return mvk.release();
  }

 protected:
  void PrepareArguments() {
    add_req_.reset(new user_data_auth::AddKeyRequest);
    check_req_.reset(new user_data_auth::CheckKeyRequest);
    mount_req_.reset(new user_data_auth::MountRequest);
    remove_req_.reset(new user_data_auth::RemoveKeyRequest);
    list_keys_req_.reset(new user_data_auth::ListKeysRequest);
  }

  template <class ProtoBuf>
  brillo::Blob BlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::BlobFromString(serialized);
  }

  template <class ProtoBuf>
  brillo::SecureBlob SecureBlobFromProtobuf(const ProtoBuf& pb) {
    std::string serialized;
    CHECK(pb.SerializeToString(&serialized));
    return brillo::SecureBlob(serialized);
  }

  std::unique_ptr<user_data_auth::AddKeyRequest> add_req_;
  std::unique_ptr<user_data_auth::CheckKeyRequest> check_req_;
  std::unique_ptr<user_data_auth::MountRequest> mount_req_;
  std::unique_ptr<user_data_auth::RemoveKeyRequest> remove_req_;
  std::unique_ptr<user_data_auth::ListKeysRequest> list_keys_req_;

 private:
  DISALLOW_COPY_AND_ASSIGN(UserDataAuthExTest);
};

TEST_F(UserDataAuthExTest, MountInvalidArgs) {
  // Note that this test doesn't distinguish between different causes of invalid
  // argument, that is, this doesn't check that
  // CRYPTOHOME_ERROR_INVALID_ARGUMENT is coming back because of the right
  // reason. This is because in the current structuring of the code, it would
  // not be possible to distinguish between those cases. This test only checks
  // that parameters that should lead to invalid argument does indeed lead to
  // invalid argument error.

  bool called;

  // This calls DoMount and check that the result is reported (i.e. the callback
  // is called), and is CRYPTOHOME_ERROR_INVALID_ARGUMENT.
  auto CallDoMountAndCheckResultIsInvalidArgument = [&called, this]() {
    called = false;
    userdataauth_.DoMount(
        *mount_req_,
        base::BindOnce(
            [](bool* called_ptr, const user_data_auth::MountReply& reply) {
              *called_ptr = true;
              EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT,
                        reply.error());
            },
            base::Unretained(&called)));
    EXPECT_TRUE(called);
  };

  // Test for case with no email.
  PrepareArguments();

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for case with no secrets.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for case with empty secret.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("");

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for create request given but without key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create();

  CallDoMountAndCheckResultIsInvalidArgument();

  // Test for create request given but with an empty key.
  PrepareArguments();
  mount_req_->mutable_account()->set_account_id("foo@gmail.com");
  mount_req_->mutable_authorization()->mutable_key()->set_secret("blerg");
  mount_req_->mutable_create()->add_keys();
  // TODO(wad) Add remaining missing field tests and NULL tests

  CallDoMountAndCheckResultIsInvalidArgument();
}

TEST_F(UserDataAuthExTest, MountPublicWithExistingMounts) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();
  SetupMount("foo@gmail.com");

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);

  bool called = false;
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  userdataauth_.DoMount(
      *mount_req_,
      base::BindOnce(
          [](bool* called_ptr, const user_data_auth::MountReply& reply) {
            *called_ptr = true;
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_MOUNT_MOUNT_POINT_BUSY,
                      reply.error());
          },
          base::Unretained(&called)));

  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, MountPublicUsesPublicMountPasskey) {
  constexpr char kUser[] = "chromeos-user";
  PrepareArguments();

  mount_req_->mutable_account()->set_account_id(kUser);
  mount_req_->set_public_mount(true);
  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(testing::InvokeWithoutArgs([&]() {
    SetupMount(kUser);
    EXPECT_CALL(*mount_, MountCryptohome(_, _, _))
        .WillOnce(testing::Invoke([](const Credentials& credentials,
                                     const Mount::MountArgs& mount_args,
                                     MountError* error) {
          brillo::SecureBlob passkey;
          credentials.GetPasskey(&passkey);
          // Tests that the passkey is filled when public_mount is set.
          EXPECT_FALSE(passkey.empty());
          return true;
        }));
    return true;
  }));

  bool called = false;
  userdataauth_.DoMount(
      *mount_req_,
      base::BindOnce(
          [](bool* called_ptr, const user_data_auth::MountReply& reply) {
            *called_ptr = true;
            EXPECT_EQ(user_data_auth::CRYPTOHOME_ERROR_NOT_SET, reply.error());
          },
          base::Unretained(&called)));

  EXPECT_TRUE(called);
}

TEST_F(UserDataAuthExTest, AddKeyInvalidArgs) {
  PrepareArguments();

  // Test for when there's no email supplied.
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for when there's no secret.
  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for when there's no new key.
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->clear_key();
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);

  // Test for no new key label.
  add_req_->mutable_key();
  // No label
  add_req_->mutable_key()->set_secret("some secret");
  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_INVALID_ARGUMENT);
}

TEST_F(UserDataAuthExTest, AddKeySanity) {
  PrepareArguments();

  add_req_->mutable_account_id()->set_account_id("foo@gmail.com");
  add_req_->mutable_authorization_request()->mutable_key()->set_secret("blerg");
  add_req_->mutable_key();
  add_req_->mutable_key()->set_secret("some secret");
  add_req_->mutable_key()->mutable_data()->set_label("just a label");

  EXPECT_CALL(homedirs_, Exists(_)).WillOnce(Return(true));
  EXPECT_CALL(homedirs_, AddKeyset(_, _, _, _, _))
      .WillOnce(Return(cryptohome::CRYPTOHOME_ERROR_NOT_SET));

  EXPECT_EQ(userdataauth_.AddKey(*add_req_.get()),
            user_data_auth::CRYPTOHOME_ERROR_NOT_SET);
}

}  // namespace cryptohome
