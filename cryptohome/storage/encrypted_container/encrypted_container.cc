// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/encrypted_container.h"

#include <memory>

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/encrypted_container/fscrypt_container.h"

namespace cryptohome {

// static
std::unique_ptr<EncryptedContainer> EncryptedContainer::Generate(
    EncryptedContainerType type,
    const base::FilePath& backing_dir,
    const FileSystemKeyReference& key_reference,
    Platform* platform) {
  switch (type) {
    case EncryptedContainerType::kFscrypt:
      return std::make_unique<FscryptContainer>(backing_dir, key_reference,
                                                platform);
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
