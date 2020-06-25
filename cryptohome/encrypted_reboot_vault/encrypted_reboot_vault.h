// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_
#define CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_

#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

#include <cryptohome/dircrypto_util.h>

class EncryptedRebootVault {
 public:
  EncryptedRebootVault();
  ~EncryptedRebootVault() = default;
  // Check if the encrypted reboot vault is setup correctly.
  bool Validate();
  // Unconditionally reset vault.
  bool CreateVault();
  // Setup existing vault; purge on failure.
  bool UnlockVault();
  // Purge vault.
  bool PurgeVault();

 private:
  base::FilePath vault_path_;
  dircrypto::KeyReference key_reference_;
};

#endif  // CRYPTOHOME_ENCRYPTED_REBOOT_VAULT_ENCRYPTED_REBOOT_VAULT_H_
