// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/encrypted_reboot_vault/encrypted_reboot_vault.h"

#include <cryptohome/cryptolib.h>
#include <cryptohome/dircrypto_util.h>
#include <cryptohome/platform.h>
#include <cryptohome/storage/encrypted_container/filesystem_key.h>
#include <cryptohome/storage/encrypted_container/fscrypt_container.h>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/key_value_store.h>

namespace {
// Pstore-pmsg path.
const char kPmsgDevicePath[] = "/dev/pmsg0";
// There can be multiple pmsg ramoops entries.
const char kPmsgKeystoreRamoopsPathDesc[] = "pmsg-ramoops-*";
const char kExt4DircryptoSupportedPath[] = "/sys/fs/ext4/features/encryption";
const char kEncryptedRebootVaultPath[] = "/mnt/stateful_partition/reboot_vault";
// Pstore path.
const char kPstorePath[] = "/sys/fs/pstore";
// Key tag to retrieve the key from pstore-pmsg.
const char kEncryptionKeyTag[] = "pmsg-key";
// Encryption key size.
const size_t kEncryptionKeySize = 64;

bool IsSupported() {
  if (!base::PathExists(base::FilePath(kPmsgDevicePath))) {
    LOG(ERROR) << "pmsg0 not enabled.";
    return false;
  }

  // Check if we can create an encrypted vault.
  if (!base::PathExists(base::FilePath(kExt4DircryptoSupportedPath))) {
    LOG(ERROR) << "ext4 directory encryption not supported.";
    return false;
  }
  return true;
}

bool SaveKey(const cryptohome::FileSystemKey& key) {
  // Do not use store.Save() since it uses WriteFileAtomically() which will
  // fail on /dev/pmsg0.
  brillo::KeyValueStore store;
  store.SetString(kEncryptionKeyTag,
                  cryptohome::CryptoLib::SecureBlobToHex(key.fek));

  std::string store_contents = store.SaveToString();
  if (store_contents.empty() ||
      !base::WriteFile(base::FilePath(kPmsgDevicePath), store_contents.data(),
                       store_contents.size())) {
    return false;
  }
  return true;
}

cryptohome::FileSystemKey RetrieveKey() {
  cryptohome::FileSystemKey key;
  base::FileEnumerator pmsg_ramoops_enumerator(
      base::FilePath(kPstorePath), true /* recursive */,
      base::FileEnumerator::FILES, kPmsgKeystoreRamoopsPathDesc);

  for (base::FilePath ramoops_file = pmsg_ramoops_enumerator.Next();
       !ramoops_file.empty(); ramoops_file = pmsg_ramoops_enumerator.Next()) {
    brillo::KeyValueStore store;
    std::string val;
    if (store.Load(ramoops_file) && store.GetString(kEncryptionKeyTag, &val)) {
      key.fek = brillo::SecureHexToSecureBlob(brillo::SecureBlob(val));
      base::DeleteFile(ramoops_file);
      // SaveKey stores the key again into pstore-pmsg on every boot since the
      // pstore object isn't persistent. Since the pstore object is always
      // stored in RAM on ChromiumOS, it is cleared the next time the device
      // shuts down or loses power.
      if (!SaveKey(key))
        LOG(WARNING) << "Failed to store key for next reboot.";
      return key;
    }
  }
  return key;
}

}  // namespace

EncryptedRebootVault::EncryptedRebootVault()
    : vault_path_(base::FilePath(kEncryptedRebootVaultPath)) {
  cryptohome::FileSystemKeyReference key_reference;
  key_reference.fek_sig = brillo::SecureBlob(kEncryptionKeyTag);

  encrypted_container_ = std::make_unique<cryptohome::FscryptContainer>(
      vault_path_, key_reference, &platform_);
}

bool EncryptedRebootVault::CreateVault() {
  if (!IsSupported()) {
    LOG(ERROR) << "EncryptedRebootVault not supported";
    return false;
  }

  base::ScopedClosureRunner reset_vault(
      base::Bind(base::IgnoreResult(&EncryptedRebootVault::PurgeVault),
                 base::Unretained(this)));

  // Remove the existing vault.
  PurgeVault();

  // Generate encryption key.
  cryptohome::FileSystemKey transient_encryption_key;
  transient_encryption_key.fek =
      cryptohome::CryptoLib::CreateSecureRandomBlob(kEncryptionKeySize);

  // Store key into pmsg. If it fails, we bail out.
  if (!SaveKey(transient_encryption_key)) {
    LOG(ERROR) << "Failed to store transient encryption key to pmsg.";
    return false;
  }

  // Set up the encrypted reboot vault.
  if (!encrypted_container_->Setup(transient_encryption_key, /*create=*/true)) {
    LOG(ERROR) << "Failed to setup encrypted container";
    return false;
  }

  ignore_result(reset_vault.Release());
  return true;
}

bool EncryptedRebootVault::Validate() {
  return base::PathExists(vault_path_) &&
         dircrypto::GetDirectoryKeyState(vault_path_) ==
             dircrypto::KeyState::ENCRYPTED;
}

bool EncryptedRebootVault::PurgeVault() {
  if (!encrypted_container_->Teardown()) {
    LOG(WARNING) << "Failed to unlink encryption key from keyring.";
  }
  return encrypted_container_->Purge();
}

bool EncryptedRebootVault::UnlockVault() {
  if (!IsSupported()) {
    LOG(ERROR) << "EncryptedRebootVault depends on pstore-pmsg to pass the "
                  "encryption key. Enable CONFIG_PSTORE_PMSG";
    return false;
  }

  // We reset the vault if we fail to unlock it for any reason.
  base::ScopedClosureRunner reset_vault(
      base::Bind(base::IgnoreResult(&EncryptedRebootVault::PurgeVault),
                 base::Unretained(this)));

  if (!Validate()) {
    LOG(ERROR) << "Invalid vault; purging.";
    return false;
  }

  // Retrieve key.
  cryptohome::FileSystemKey transient_encryption_key = RetrieveKey();
  if (transient_encryption_key.fek.empty()) {
    LOG(INFO) << "No valid key found: the device might have booted up from a "
                 "shutdown.";
    return false;
  }

  // Unlock vault.
  if (!encrypted_container_->Setup(transient_encryption_key,
                                   /*create=*/false)) {
    LOG(ERROR) << "Failed to add key to keyring.";
    return false;
  }

  ignore_result(reset_vault.Release());
  return true;
}
