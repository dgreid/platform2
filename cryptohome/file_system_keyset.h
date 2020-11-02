// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_FILE_SYSTEM_KEYSET_H_
#define CRYPTOHOME_FILE_SYSTEM_KEYSET_H_

#include <brillo/secure_blob.h>

#include <string>

#include "cryptohome/encrypted_container/filesystem_key.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

// This class wraps the file encryption keys and serves as a common interface
// across mount and authentication operations for these keys.
class FileSystemKeyset final {
 public:
  FileSystemKeyset();
  explicit FileSystemKeyset(const cryptohome::VaultKeyset& vault_keyset);
  ~FileSystemKeyset();

  // Getters for the associated key data
  const FileSystemKey Key() const;
  const FileSystemKeyReference KeyReference() const;
  const brillo::SecureBlob& chaps_key() const;

 private:
  // Keys for file encryption. Currently we would need file_encryption_key(fek)
  // file_name_encryption_key (fnek), fek_salt, fnek_salt, fek_sig, fnek_sig.
  // The fnek keys are used only in the older Ecryptfs operations.
  FileSystemKey key_;
  FileSystemKeyReference key_reference_;

  // Chaps keys are stored in keysets right now and are used as part of mount
  // operations.
  brillo::SecureBlob chaps_key_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_FILE_SYSTEM_KEYSET_H_
