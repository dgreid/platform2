// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device_factory.h"
#include "cryptohome/storage/encrypted_container/dmcrypt_container.h"
#include "cryptohome/storage/encrypted_container/ecryptfs_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/encrypted_container/fscrypt_container.h"

namespace cryptohome {

EncryptedContainerFactory::EncryptedContainerFactory(Platform* platform)
    : EncryptedContainerFactory(
          platform, std::make_unique<BackingDeviceFactory>(platform)) {}

EncryptedContainerFactory::EncryptedContainerFactory(
    Platform* platform,
    std::unique_ptr<BackingDeviceFactory> backing_device_factory)
    : platform_(platform),
      backing_device_factory_(std::move(backing_device_factory)) {}

std::unique_ptr<EncryptedContainer> EncryptedContainerFactory::Generate(
    const EncryptedContainerConfig& config,
    const FileSystemKeyReference& key_reference) {
  switch (config.type) {
    case EncryptedContainerType::kFscrypt:
      return std::make_unique<FscryptContainer>(config.backing_dir,
                                                key_reference, platform_);
    case EncryptedContainerType::kEcryptfs:
      return std::make_unique<EcryptfsContainer>(config.backing_dir,
                                                 key_reference, platform_);
    case EncryptedContainerType::kDmcrypt:
      return std::make_unique<DmcryptContainer>(
          config.dmcrypt_config,
          backing_device_factory_->Generate(
              config.dmcrypt_config.backing_device_config),
          key_reference, platform_);
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
