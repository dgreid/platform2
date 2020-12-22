// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_
#define CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_

#include <memory>
#include <string>

#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/mount_constants.h"

namespace cryptohome {

// A cryptohome vault represents the user's active encrypted containers that
// comprise the user's home directory and handles operations relating to setting
// up the user's home directory for mount and tearing down the encrypted
// containers after unmount.
//
// Note that the mount arguments passed to the vault define the setup/teardown
// functions. This is intentional: it allows use of a deterministic teardown
// function on restart from a crash.
class CryptohomeVault {
 public:
  struct Options {
    // Forces the type of new encrypted containers set up.
    EncryptedContainerType force_type = EncryptedContainerType::kUnknown;
    // Checks if migration should be allowed for the current vault. Currently,
    // this is only used for ecryptfs.
    bool migrate = false;
    // Checks if mount requests for ecryptfs mounts should be blocked without
    // migration.
    bool block_ecryptfs = false;
  };
  CryptohomeVault(const std::string& obfuscated_username,
                  std::unique_ptr<EncryptedContainer> container,
                  std::unique_ptr<EncryptedContainer> migrating_container,
                  Platform* platform);
  ~CryptohomeVault();

  // Sets up the cryptohome vault for mounting.
  MountError Setup(const FileSystemKey& filesystem_key, bool create);

  // Tears down the vault post-unmount.
  bool Teardown();

  // Get mount type for mount to use.
  MountType GetMountType();

  void ReportVaultEncryptionType();

  EncryptedContainerType GetContainerType() {
    return container_ ? container_->GetType()
                      : EncryptedContainerType::kUnknown;
  }
  EncryptedContainerType GetMigratingContainerType() {
    return migrating_container_ ? migrating_container_->GetType()
                                : EncryptedContainerType::kUnknown;
  }

 private:
  friend class CryptohomeVaultTest;

  const std::string obfuscated_username_;

  // Represents the active encrypted container for the vault.
  std::unique_ptr<EncryptedContainer> container_;
  // During migration, we set up the target migration container as
  // |migrating_container_|.
  std::unique_ptr<EncryptedContainer> migrating_container_;

  Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_CRYPTOHOME_VAULT_H_
