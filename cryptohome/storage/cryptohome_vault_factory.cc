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
    const FileSystemKeyReference& key_reference) {
  EncryptedContainerConfig config;

  switch (type) {
    case EncryptedContainerType::kEcryptfs:
      config.backing_dir = GetEcryptfsUserVaultPath(obfuscated_username);
      config.type = EncryptedContainerType::kEcryptfs;
      break;
    case EncryptedContainerType::kFscrypt:
      config.backing_dir = GetUserMountDirectory(obfuscated_username);
      config.type = EncryptedContainerType::kFscrypt;
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
  std::unique_ptr<EncryptedContainer> container = GenerateEncryptedContainer(
      container_type, obfuscated_username, key_reference);
  std::unique_ptr<EncryptedContainer> migrating_container =
      GenerateEncryptedContainer(migrating_container_type, obfuscated_username,
                                 key_reference);

  return std::make_unique<CryptohomeVault>(
      obfuscated_username, std::move(container), std::move(migrating_container),
      platform_);
}

}  // namespace cryptohome
