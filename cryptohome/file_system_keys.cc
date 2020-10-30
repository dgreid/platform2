// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/file_system_keys.h"

#include <brillo/cryptohome.h>

using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

FileSystemKeys::FileSystemKeys() = default;
FileSystemKeys::~FileSystemKeys() = default;

FileSystemKeys::FileSystemKeys(const cryptohome::VaultKeyset& vault_keyset) {
  fek_ = vault_keyset.fek();
  fek_sig_ = vault_keyset.fek_sig();
  fek_salt_ = vault_keyset.fek_salt();
  fnek_ = vault_keyset.fnek();
  fnek_salt_ = vault_keyset.fnek_salt();
  fnek_sig_ = vault_keyset.fnek_sig();
  chaps_key_ = vault_keyset.chaps_key();
}

const brillo::SecureBlob& FileSystemKeys::fek() const {
  return fek_;
}

const brillo::SecureBlob& FileSystemKeys::fnek() const {
  return fnek_;
}

const brillo::SecureBlob& FileSystemKeys::fek_salt() const {
  return fek_salt_;
}

const brillo::SecureBlob& FileSystemKeys::fnek_salt() const {
  return fnek_salt_;
}
const brillo::SecureBlob& FileSystemKeys::fek_sig() const {
  return fek_sig_;
}

const brillo::SecureBlob& FileSystemKeys::fnek_sig() const {
  return fnek_sig_;
}

const brillo::SecureBlob& FileSystemKeys::chaps_key() const {
  return chaps_key_;
}

}  // namespace cryptohome
