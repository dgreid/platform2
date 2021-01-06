// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FSCRYPT_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FSCRYPT_CONTAINER_H_

#include "cryptohome/storage/encrypted_container/encrypted_container.h"

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

// `FscryptContainer` is a file-level encrypted container which uses fscrypt to
// encrypt the `backing_dir_` transparently.
class FscryptContainer : public EncryptedContainer {
 public:
  FscryptContainer(const base::FilePath& backing_dir,
                   const FileSystemKeyReference& key_reference,
                   Platform* platform);
  ~FscryptContainer() = default;

  bool Setup(const FileSystemKey& encryption_key, bool create) override;
  bool Teardown() override;
  bool Exists() override;
  bool Purge() override;
  EncryptedContainerType GetType() override {
    return EncryptedContainerType::kFscrypt;
  }

 private:
  const base::FilePath backing_dir_;
  dircrypto::KeyReference key_reference_;
  Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FSCRYPT_CONTAINER_H_
