// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/cryptohome_vault_factory.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/cryptohome_vault.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace {
// Size of logical volumes to use for the dm-crypt cryptohomes.
constexpr uint64_t kLogicalVolumeSizePercent = 90;
}  // namespace

namespace cryptohome {

CryptohomeVaultFactory::CryptohomeVaultFactory(
    Platform* platform,
    std::unique_ptr<EncryptedContainerFactory> encrypted_container_factory)
    : platform_(platform),
      encrypted_container_factory_(std::move(encrypted_container_factory)) {}

CryptohomeVaultFactory::CryptohomeVaultFactory(Platform* platform)
    : CryptohomeVaultFactory(
          platform, std::make_unique<EncryptedContainerFactory>(platform)) {}

CryptohomeVaultFactory::~CryptohomeVaultFactory() {}

std::unique_ptr<EncryptedContainer>
CryptohomeVaultFactory::GenerateEncryptedContainer(
    EncryptedContainerType type,
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    const std::string& container_identifier) {
  EncryptedContainerConfig config;
  base::FilePath stateful_device;
  uint64_t stateful_size;

  switch (type) {
    case EncryptedContainerType::kEcryptfs:
      config.backing_dir = GetEcryptfsUserVaultPath(obfuscated_username);
      config.type = EncryptedContainerType::kEcryptfs;
      break;
    case EncryptedContainerType::kFscrypt:
      config.backing_dir = GetUserMountDirectory(obfuscated_username);
      config.type = EncryptedContainerType::kFscrypt;
      break;
    case EncryptedContainerType::kDmcrypt:
      // Calculate size for dm-crypt partition.
      stateful_device = platform_->GetStatefulDevice();
      if (stateful_device.empty())
        return nullptr;

      if (!platform_->GetBlkSize(stateful_device, &stateful_size))
        return nullptr;

      config.type = EncryptedContainerType::kDmcrypt;
      config.dmcrypt_config = {
          .backing_device_config =
              {.type = BackingDeviceType::kLogicalVolumeBackingDevice,
               .name = LogicalVolumePrefix(obfuscated_username) +
                       container_identifier,
               .size = (stateful_size * kLogicalVolumeSizePercent) /
                       (100 * 1024 * 1024),
               .logical_volume = {.thinpool_name = "thinpool",
                                  .physical_volume = stateful_device}},
          .dmcrypt_device_name =
              DmcryptVolumePrefix(obfuscated_username) + container_identifier,
          .dmcrypt_cipher = "aes-xts-plain64",
          // TODO(sarthakkukreti): Add more dynamic checks for filesystem
          // features once dm-crypt cryptohomes are stable.
          .mkfs_opts = {"-O", "^huge_file,^flex_bg,", "-E",
                        "discard,lazy_itable_init"},
          .tune2fs_opts = {"-O", "verity,quota", "-Q", "usrquota,grpquota"}};
      break;
    default:
      return nullptr;
  }

  return encrypted_container_factory_->Generate(config, key_reference);
}

std::unique_ptr<CryptohomeVault> CryptohomeVaultFactory::Generate(
    const std::string& obfuscated_username,
    const FileSystemKeyReference& key_reference,
    EncryptedContainerType container_type,
    EncryptedContainerType migrating_container_type) {
  // Generate containers for the vault.
  std::unique_ptr<EncryptedContainer> container =
      GenerateEncryptedContainer(container_type, obfuscated_username,
                                 key_reference, kDmcryptDataContainerSuffix);
  std::unique_ptr<EncryptedContainer> migrating_container =
      GenerateEncryptedContainer(migrating_container_type, obfuscated_username,
                                 key_reference, kDmcryptDataContainerSuffix);

  std::unique_ptr<EncryptedContainer> cache_container;
  if (container_type == EncryptedContainerType::kDmcrypt) {
    cache_container =
        GenerateEncryptedContainer(container_type, obfuscated_username,
                                   key_reference, kDmcryptCacheContainerSuffix);
  }
  return std::make_unique<CryptohomeVault>(
      obfuscated_username, std::move(container), std::move(migrating_container),
      std::move(cache_container), platform_);
}

}  // namespace cryptohome
