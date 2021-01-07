// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_FACTORY_H_
#define CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_FACTORY_H_

#include "cryptohome/storage/cryptohome_vault.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

class CryptohomeVaultFactory {
 public:
  CryptohomeVaultFactory(
      Platform* platform,
      std::unique_ptr<EncryptedContainerFactory> encrypted_container_factory);
  explicit CryptohomeVaultFactory(Platform* platform);

  virtual ~CryptohomeVaultFactory();

  virtual std::unique_ptr<CryptohomeVault> Generate(
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      EncryptedContainerType container_type,
      EncryptedContainerType migrating_container_type);

 private:
  virtual std::unique_ptr<EncryptedContainer> GenerateEncryptedContainer(
      EncryptedContainerType type,
      const std::string& obfuscated_username,
      const FileSystemKeyReference& key_reference,
      const std::string& container_identifier);

  Platform* platform_;
  std::unique_ptr<EncryptedContainerFactory> encrypted_container_factory_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_FACTORY_H_
