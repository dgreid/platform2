// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ecryptfs_container.h"

#include <base/files/file_path.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

EcryptfsContainer::EcryptfsContainer(
    const base::FilePath& backing_dir,
    const FileSystemKeyReference& key_reference,
    Platform* platform)
    : backing_dir_(backing_dir),
      key_reference_(key_reference),
      platform_(platform) {}

bool EcryptfsContainer::Purge() {
  return platform_->DeletePathRecursively(backing_dir_);
}

bool EcryptfsContainer::Setup(const FileSystemKey& encryption_key,
                              bool create) {
  if (create) {
    if (!platform_->CreateDirectory(backing_dir_)) {
      LOG(ERROR) << "Failed to create backing directory";
      return false;
    }
  }

  // Add the File Encryption key (FEK) from the vault keyset.  This is the key
  // that is used to encrypt the file contents when the file is persisted to the
  // lower filesystem by eCryptfs.
  auto key_signature = CryptoLib::SecureBlobToHex(key_reference_.fek_sig);
  if (!platform_->AddEcryptfsAuthToken(encryption_key.fek, key_signature,
                                       encryption_key.fek_salt)) {
    LOG(ERROR) << "Couldn't add eCryptfs file encryption key to keyring.";
    return false;
  }

  // Add the File Name Encryption Key (FNEK) from the vault keyset.  This is the
  // key that is used to encrypt the file name when the file is persisted to the
  // lower filesystem by eCryptfs.
  auto filename_key_signature =
      CryptoLib::SecureBlobToHex(key_reference_.fnek_sig);
  if (!platform_->AddEcryptfsAuthToken(encryption_key.fnek,
                                       filename_key_signature,
                                       encryption_key.fnek_salt)) {
    LOG(ERROR) << "Couldn't add eCryptfs filename encryption key to keyring.";
    return false;
  }
  return true;
}

bool EcryptfsContainer::Teardown() {
  return platform_->ClearUserKeyring();
}

}  // namespace cryptohome
