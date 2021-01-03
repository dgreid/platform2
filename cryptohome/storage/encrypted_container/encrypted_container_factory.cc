// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/encrypted_container_factory.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/ecryptfs_container.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/encrypted_container/fscrypt_container.h"

namespace cryptohome {

EncryptedContainerFactory::EncryptedContainerFactory(Platform* platform)
    : platform_(platform) {}

std::unique_ptr<EncryptedContainer> EncryptedContainerFactory::Generate(
    EncryptedContainerType type,
    const base::FilePath& backing_dir,
    const FileSystemKeyReference& key_reference) {
  switch (type) {
    case EncryptedContainerType::kFscrypt:
      return std::make_unique<FscryptContainer>(backing_dir, key_reference,
                                                platform_);
    case EncryptedContainerType::kEcryptfs:
      return std::make_unique<EcryptfsContainer>(backing_dir, key_reference,
                                                 platform_);
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
