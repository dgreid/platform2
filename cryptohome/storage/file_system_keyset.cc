// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/file_system_keyset.h"

#include <brillo/cryptohome.h>

using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

FileSystemKeyset::FileSystemKeyset() = default;
FileSystemKeyset::~FileSystemKeyset() = default;

FileSystemKeyset::FileSystemKeyset(
    const cryptohome::VaultKeyset& vault_keyset) {
  key_.fek = vault_keyset.fek();
  key_.fek_salt = vault_keyset.fek_salt();
  key_.fnek = vault_keyset.fnek();
  key_.fnek_salt = vault_keyset.fnek_salt();

  key_reference_.fek_sig = vault_keyset.fek_sig();
  key_reference_.fnek_sig = vault_keyset.fnek_sig();

  chaps_key_ = vault_keyset.chaps_key();
}

const FileSystemKey FileSystemKeyset::Key() const {
  return key_;
}

const FileSystemKeyReference FileSystemKeyset::KeyReference() const {
  return key_reference_;
}

const brillo::SecureBlob& FileSystemKeyset::chaps_key() const {
  return chaps_key_;
}

}  // namespace cryptohome
