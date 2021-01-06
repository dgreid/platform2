// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/fscrypt_container.h"

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

namespace cryptohome {

FscryptContainer::FscryptContainer(const base::FilePath& backing_dir,
                                   const FileSystemKeyReference& key_reference,
                                   Platform* platform)
    : backing_dir_(backing_dir),
      key_reference_({.reference = key_reference.fek_sig}),
      platform_(platform) {}

bool FscryptContainer::Purge() {
  return platform_->DeletePathRecursively(backing_dir_);
}

bool FscryptContainer::Exists() {
  return platform_->DirectoryExists(backing_dir_) &&
         platform_->GetDirCryptoKeyState(backing_dir_) ==
             dircrypto::KeyState::ENCRYPTED;
}

bool FscryptContainer::Setup(const FileSystemKey& encryption_key, bool create) {
  if (create) {
    if (!platform_->CreateDirectory(backing_dir_)) {
      LOG(ERROR) << "Failed to create directory " << backing_dir_;
      return false;
    }
  }

  key_reference_.policy_version =
      dircrypto::GetDirectoryPolicyVersion(backing_dir_);

  if (key_reference_.policy_version < 0) {
    key_reference_.policy_version = dircrypto::CheckFscryptKeyIoctlSupport()
                                        ? FSCRYPT_POLICY_V2
                                        : FSCRYPT_POLICY_V1;
  }

  if (!platform_->AddDirCryptoKeyToKeyring(encryption_key.fek,
                                           &key_reference_)) {
    LOG(ERROR) << "Failed to add fscrypt key to kernel";
    return false;
  }

  // `SetDirectoryKey` is a set-or-verify function: for directories with the
  // encryption policy already set, this function call acts as a verifier.
  if (!platform_->SetDirCryptoKey(backing_dir_, key_reference_)) {
    LOG(ERROR) << "Failed to set fscrypt key for backing directory";
    return false;
  }

  return true;
}

bool FscryptContainer::Teardown() {
  return platform_->InvalidateDirCryptoKey(key_reference_, backing_dir_);
}

}  // namespace cryptohome
