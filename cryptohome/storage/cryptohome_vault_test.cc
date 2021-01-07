// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cryptohome/storage/cryptohome_vault.h>

#include <memory>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/backing_device_factory.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/encrypted_container/fake_encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/mock_homedirs.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace cryptohome {

namespace {
struct CryptohomeVaultTestParams {
  CryptohomeVaultTestParams(EncryptedContainerType type,
                            EncryptedContainerType migrating_type,
                            EncryptedContainerType cache_type)
      : container_type(type),
        migrating_container_type(migrating_type),
        cache_container_type(cache_type) {}

  EncryptedContainerType container_type;
  EncryptedContainerType migrating_container_type;
  EncryptedContainerType cache_container_type;
};
}  // namespace

class CryptohomeVaultTest
    : public ::testing::TestWithParam<CryptohomeVaultTestParams> {
 public:
  CryptohomeVaultTest()
      : obfuscated_username_("foo"),
        key_reference_({.fek_sig = brillo::SecureBlob("random keyref")}),
        key_({.fek = brillo::SecureBlob("random key")}),
        backing_dir_(ShadowRoot().Append(obfuscated_username_)),
        encrypted_container_factory_(&platform_) {}
  ~CryptohomeVaultTest() override = default;

  EncryptedContainerType ContainerType() { return GetParam().container_type; }

  EncryptedContainerType MigratingContainerType() {
    return GetParam().migrating_container_type;
  }

  EncryptedContainerType CacheContainerType() {
    return GetParam().cache_container_type;
  }

  EncryptedContainerConfig ConfigFromType(EncryptedContainerType type,
                                          const std::string& name) {
    EncryptedContainerConfig config;
    config.type = type;
    switch (type) {
      case EncryptedContainerType::kEcryptfs:
        config.backing_dir = backing_dir_.Append(kEcryptfsVaultDir);
        break;
      case EncryptedContainerType::kFscrypt:
        config.backing_dir = backing_dir_.Append(kMountDir);
        break;
      case EncryptedContainerType::kDmcrypt:
        config = {
            .type = EncryptedContainerType::kDmcrypt,
            .dmcrypt_config = {
                .backing_device_config =
                    {.type = BackingDeviceType::kLogicalVolumeBackingDevice,
                     .name = name,
                     .size = 100 * 1024 * 1024,
                     .logical_volume = {.thinpool_name = "thinpool",
                                        .physical_volume =
                                            base::FilePath("/dev/sda1")}},
                .dmcrypt_device_name = "dmcrypt-" + name,
                .dmcrypt_cipher = "aes-xts-plain64",
                .mkfs_opts = {"-O", "^huge_file,^flex_bg,", "-E",
                              "discard,lazy_itable_init"},
                .tune2fs_opts = {"-O", "verity,quota", "-Q",
                                 "usrquota,grpquota"}}};
        break;
      default:
        config.type = EncryptedContainerType::kUnknown;
        break;
    }

    return config;
  }

  void ExpectEcryptfsSetup() {
    EXPECT_CALL(platform_, AddEcryptfsAuthToken(_, _, _))
        .Times(2)
        .WillRepeatedly(Return(true));
  }

  void ExpectFscryptSetup() {
    EXPECT_CALL(platform_, AddDirCryptoKeyToKeyring(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, SetDirCryptoKey(backing_dir_.Append(kMountDir), _))
        .WillOnce(Return(true));
  }

  void ExpectDmcryptSetup(const std::string& name) {
    base::FilePath backing_device_path = base::FilePath("/dev").Append(name);
    base::FilePath dmcrypt_device("/dev/mapper/dmcrypt-" + name);
    EXPECT_CALL(platform_, GetBlkSize(backing_device_path, _))
        .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
    EXPECT_CALL(platform_, UdevAdmSettle(dmcrypt_device, _))
        .WillOnce(Return(true));
    EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(platform_, Tune2Fs(dmcrypt_device, _)).WillOnce(Return(true));
  }

  void ExpectEcryptfsTeardown() {
    EXPECT_CALL(platform_, ClearUserKeyring()).WillRepeatedly(Return(true));
  }

  void ExpectFscryptTeardown() {
    EXPECT_CALL(platform_,
                InvalidateDirCryptoKey(_, backing_dir_.Append(kMountDir)))
        .WillRepeatedly(Return(true));
  }

  void ExpectContainerSetup(EncryptedContainerType type) {
    switch (type) {
      case EncryptedContainerType::kEcryptfs:
        ExpectEcryptfsSetup();
        break;
      case EncryptedContainerType::kFscrypt:
        ExpectFscryptSetup();
        break;
      case EncryptedContainerType::kDmcrypt:
        ExpectDmcryptSetup("data");
        break;
      default:
        break;
    }
  }

  void ExpectCacheContainerSetup(EncryptedContainerType type) {
    switch (type) {
      case EncryptedContainerType::kDmcrypt:
        ExpectDmcryptSetup("cache");
        break;
      default:
        break;
    }
  }

  void ExpectContainerTeardown(EncryptedContainerType type) {
    switch (type) {
      case EncryptedContainerType::kEcryptfs:
        ExpectEcryptfsTeardown();
        break;
      case EncryptedContainerType::kFscrypt:
        ExpectFscryptTeardown();
        break;
      default:
        break;
    }
  }

  void CreateExistingContainer(EncryptedContainerType type) {
    switch (type) {
      case EncryptedContainerType::kEcryptfs:
        platform_.CreateDirectory(backing_dir_.Append(kEcryptfsVaultDir));
        break;
      case EncryptedContainerType::kFscrypt:
        platform_.CreateDirectory(backing_dir_.Append(kMountDir));
        break;
      default:
        break;
    }
  }

  void CheckContainersExist() {
    // For newly created fscrypt containers, add the expectation that fetching
    // the key state returns encrypted.
    if (vault_->container_->GetType() == EncryptedContainerType::kFscrypt ||
        (vault_->migrating_container_ &&
         vault_->migrating_container_->GetType() ==
             EncryptedContainerType::kFscrypt)) {
      EXPECT_CALL(platform_,
                  GetDirCryptoKeyState(backing_dir_.Append(kMountDir)))
          .WillOnce(Return(dircrypto::KeyState::ENCRYPTED));
    }
    EXPECT_TRUE(vault_->container_->Exists());
    if (vault_->migrating_container_) {
      EXPECT_TRUE(vault_->migrating_container_->Exists());
    }
    if (vault_->cache_container_) {
      EXPECT_TRUE(vault_->cache_container_->Exists());
    }
  }

  void ExpectVaultSetup() {
    EXPECT_CALL(platform_, ClearUserKeyring()).WillOnce(Return(true));
    EXPECT_CALL(platform_, SetupProcessKeyring()).WillOnce(Return(true));
  }

  void ExpectVaultTeardownOnDestruction() {
    ExpectContainerTeardown(ContainerType());
    ExpectContainerTeardown(MigratingContainerType());
    ExpectContainerTeardown(CacheContainerType());
  }

  void GenerateVault(bool create_container,
                     bool create_migrating_container,
                     bool create_cache_container) {
    std::unique_ptr<EncryptedContainer> container =
        encrypted_container_factory_.Generate(
            ConfigFromType(ContainerType(), "data"), key_reference_,
            create_container);
    if (create_container)
      CreateExistingContainer(ContainerType());

    std::unique_ptr<EncryptedContainer> migrating_container =
        encrypted_container_factory_.Generate(
            ConfigFromType(MigratingContainerType(), "data"), key_reference_,
            create_migrating_container);
    if (create_migrating_container)
      CreateExistingContainer(MigratingContainerType());

    std::unique_ptr<EncryptedContainer> cache_container =
        encrypted_container_factory_.Generate(
            ConfigFromType(CacheContainerType(), "cache"), key_reference_,
            create_cache_container);
    if (create_cache_container)
      CreateExistingContainer(CacheContainerType());

    vault_ = std::make_unique<CryptohomeVault>(
        obfuscated_username_, std::move(container),
        std::move(migrating_container), std::move(cache_container), &platform_);
  }

 protected:
  const std::string obfuscated_username_;
  const FileSystemKeyReference key_reference_;
  const FileSystemKey key_;
  const base::FilePath backing_dir_;

  MockHomeDirs homedirs_;
  MockPlatform platform_;
  FakeEncryptedContainerFactory encrypted_container_factory_;
  std::unique_ptr<CryptohomeVault> vault_;
};

INSTANTIATE_TEST_SUITE_P(WithEcryptfs,
                         CryptohomeVaultTest,
                         ::testing::Values(CryptohomeVaultTestParams(
                             EncryptedContainerType::kEcryptfs,
                             EncryptedContainerType::kUnknown,
                             EncryptedContainerType::kUnknown)));
INSTANTIATE_TEST_SUITE_P(WithFscrypt,
                         CryptohomeVaultTest,
                         ::testing::Values(CryptohomeVaultTestParams(
                             EncryptedContainerType::kFscrypt,
                             EncryptedContainerType::kUnknown,
                             EncryptedContainerType::kUnknown)));
INSTANTIATE_TEST_SUITE_P(WithFscryptMigration,
                         CryptohomeVaultTest,
                         ::testing::Values(CryptohomeVaultTestParams(
                             EncryptedContainerType::kEcryptfs,
                             EncryptedContainerType::kFscrypt,
                             EncryptedContainerType::kUnknown)));

INSTANTIATE_TEST_SUITE_P(WithDmcrypt,
                         CryptohomeVaultTest,
                         ::testing::Values(CryptohomeVaultTestParams(
                             EncryptedContainerType::kDmcrypt,
                             EncryptedContainerType::kUnknown,
                             EncryptedContainerType::kDmcrypt)));

// Tests failure path on failure to setup process keyring for eCryptfs and
// fscrypt.
TEST_P(CryptohomeVaultTest, FailedProcessKeyringSetup) {
  GenerateVault(/*create_container=*/false,
                /*create_migrating_container=*/false,
                /*create_cache_container=*/false);
  EXPECT_CALL(platform_, SetupProcessKeyring()).WillOnce(Return(false));
  EXPECT_EQ(vault_->Setup(key_, /*create=*/true),
            MOUNT_ERROR_SETUP_PROCESS_KEYRING_FAILED);
  ExpectVaultTeardownOnDestruction();
}

// Tests the failure path on Setup if setting up the container fails.
TEST_P(CryptohomeVaultTest, ContainerSetupFailed) {
  GenerateVault(/*create_container=*/false,
                /*create_migrating_container=*/false,
                /*create_cache_container=*/false);
  ExpectVaultSetup();
  EXPECT_EQ(vault_->Setup(key_, /*create=*/true), MOUNT_ERROR_KEYRING_FAILED);
  ExpectVaultTeardownOnDestruction();
}

// Tests the failure path on Setup if setting up the container fails.
TEST_P(CryptohomeVaultTest, MigratingContainerSetupFailed) {
  GenerateVault(/*create_container=*/false,
                /*create_migrating_container=*/false,
                /*create_cache_container=*/false);
  ExpectVaultSetup();
  ExpectContainerSetup(ContainerType());
  ExpectCacheContainerSetup(CacheContainerType());

  // In absence of a migrating container, the vault setup should succeed.
  MountError error =
      MigratingContainerType() != EncryptedContainerType::kUnknown
          ? MOUNT_ERROR_KEYRING_FAILED
          : MOUNT_ERROR_NONE;

  EXPECT_EQ(vault_->Setup(key_, /*create=*/true), error);
  ExpectVaultTeardownOnDestruction();
}

// Tests the setup path of a pristine cryptohome.
TEST_P(CryptohomeVaultTest, CreateVault) {
  GenerateVault(/*create_container=*/false,
                /*create_migrating_container=*/false,
                /*create_cache_container=*/false);
  ExpectVaultSetup();
  ExpectContainerSetup(ContainerType());
  ExpectContainerSetup(MigratingContainerType());
  ExpectCacheContainerSetup(CacheContainerType());

  EXPECT_EQ(vault_->Setup(key_, /*create=*/true), MOUNT_ERROR_NONE);

  CheckContainersExist();
  ExpectVaultTeardownOnDestruction();
}

// Tests the setup path for an existing container with no migrating container
// setup.
TEST_P(CryptohomeVaultTest, ExistingVaultNoMigratingVault) {
  GenerateVault(/*create_container=*/true,
                /*create_migrating_container=*/false,
                /*create_cache_container=*/false);
  ExpectVaultSetup();
  ExpectContainerSetup(ContainerType());
  ExpectContainerSetup(MigratingContainerType());
  ExpectCacheContainerSetup(CacheContainerType());

  EXPECT_EQ(vault_->Setup(key_, /*create=*/false), MOUNT_ERROR_NONE);

  CheckContainersExist();
  ExpectVaultTeardownOnDestruction();
}

// Tests the setup path for an existing vault with an existing migrating
// container (incomplete migration).
TEST_P(CryptohomeVaultTest, ExistingMigratingVault) {
  GenerateVault(/*create_container=*/true, /*create_migrating_container=*/true,
                /*create_cache_container=*/false);
  ExpectVaultSetup();
  ExpectContainerSetup(ContainerType());
  ExpectContainerSetup(MigratingContainerType());
  ExpectCacheContainerSetup(CacheContainerType());

  EXPECT_EQ(vault_->Setup(key_, /*create=*/false), MOUNT_ERROR_NONE);

  CheckContainersExist();
  ExpectVaultTeardownOnDestruction();
}

// Tests the setup path for an existing vault with an existing cache container
// (incomplete migration).
TEST_P(CryptohomeVaultTest, ExistingCacheContainer) {
  GenerateVault(/*create_container=*/true, /*create_migrating_container=*/false,
                /*create_cache_container=*/true);
  ExpectVaultSetup();
  ExpectContainerSetup(ContainerType());
  ExpectContainerSetup(MigratingContainerType());
  ExpectCacheContainerSetup(CacheContainerType());

  EXPECT_EQ(vault_->Setup(key_, /*create=*/false), MOUNT_ERROR_NONE);

  CheckContainersExist();
  ExpectVaultTeardownOnDestruction();
}

}  // namespace cryptohome
