// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/nvram_boot_lockbox.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/file_utils.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_utility.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

namespace cryptohome {

NVRamBootLockbox::NVRamBootLockbox(
    TPMNVSpaceUtilityInterface* tpm_nvspace_utility)
    : boot_lockbox_filepath_(base::FilePath(kNVRamBootLockboxFilePath)),
      tpm_nvspace_utility_(tpm_nvspace_utility) {}

NVRamBootLockbox::NVRamBootLockbox(
    TPMNVSpaceUtilityInterface* tpm_nvspace_utility,
    const base::FilePath& bootlockbox_file_path)
    : boot_lockbox_filepath_(bootlockbox_file_path),
      tpm_nvspace_utility_(tpm_nvspace_utility) {}

NVRamBootLockbox::~NVRamBootLockbox() {}

bool NVRamBootLockbox::Store(const std::string& key,
                             const std::string& digest,
                             BootLockboxErrorCode* error) {
  // Returns nvspace state to client.
  *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NOT_SET;
  if (nvspace_state_ == NVSpaceState::kNVSpaceWriteLocked) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_WRITE_LOCKED;
    return false;
  }
  if (nvspace_state_ == NVSpaceState::kNVSpaceUndefined) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NVSPACE_UNDEFINED;
    return false;
  }
  if (nvspace_state_ == NVSpaceState::kNVSpaceError) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NVSPACE_OTHER;
    return false;
  }

  // A temporaray key value map for writing.
  KeyValueMap updated_key_value_map = key_value_store_;
  updated_key_value_map[key] = digest;
  if (!FlushAndUpdate(updated_key_value_map)) {
    LOG(ERROR) << "Store Failed: Cannot flush to file.";
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_WRITE_FAILED;
    return false;
  }
  return true;
}

bool NVRamBootLockbox::Read(const std::string& key,
                            std::string* digest,
                            BootLockboxErrorCode* error) {
  // Returns nvspace state to client.
  *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NOT_SET;
  if (nvspace_state_ == NVSpaceState::kNVSpaceUndefined) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NVSPACE_UNDEFINED;
    return false;
  }
  if (nvspace_state_ == NVSpaceState::kNVSpaceUninitialized) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NVSPACE_UNINITIALIZED;
    return false;
  }
  if (nvspace_state_ == NVSpaceState::kNVSpaceError) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_NVSPACE_OTHER;
    return false;
  }

  KeyValueMap::const_iterator it = key_value_store_.find(key);
  if (it == key_value_store_.end()) {
    *error = BootLockboxErrorCode::BOOTLOCKBOX_ERROR_MISSING_KEY;
    return false;
  }
  *digest = it->second;
  return true;
}

bool NVRamBootLockbox::Finalize() {
  if (nvspace_state_ == NVSpaceState::kNVSpaceUndefined) {
    return false;
  }
  if (tpm_nvspace_utility_->LockNVSpace()) {
    nvspace_state_ = NVSpaceState::kNVSpaceWriteLocked;
    return true;
  }
  nvspace_state_ = NVSpaceState::kNVSpaceError;
  return false;
}

bool NVRamBootLockbox::DefineSpace() {
  if (tpm_nvspace_utility_->DefineNVSpace()) {
    nvspace_state_ = NVSpaceState::kNVSpaceUninitialized;
    return true;
  }
  nvspace_state_ = NVSpaceState::kNVSpaceUndefined;
  return false;
}

bool NVRamBootLockbox::Load() {
  if (!tpm_nvspace_utility_->ReadNVSpace(&root_digest_, &nvspace_state_)) {
    LOG(ERROR) << "Failed to read NVRAM space.";
    return false;
  }

  brillo::Blob data;
  if (!Platform().ReadFile(boot_lockbox_filepath_, &data)) {
    LOG(ERROR) << "Failed to read boot lockbox file.";
    return false;
  }

  brillo::Blob digest_blob = CryptoLib::Sha256(data);
  std::string digest(digest_blob.begin(), digest_blob.end());

  if (digest != root_digest_) {
    LOG(ERROR) << "The nvram boot lockbox file verification failed.";
    return false;
  }

  SerializedKeyValueMap message;
  if (!message.ParseFromArray(data.data(), data.size())) {
    LOG(ERROR) << "Failed to parse boot lockbox file.";
    return false;
  }

  if (!message.has_version() || message.version() != kVersion) {
    LOG(ERROR) << "Unsupported version " << message.version();
    return false;
  }

  KeyValueMap tmp(message.keyvals().begin(), message.keyvals().end());
  key_value_store_.swap(tmp);
  return true;
}

bool NVRamBootLockbox::FlushAndUpdate(const KeyValueMap& keyvals) {
  SerializedKeyValueMap message;
  message.set_version(kVersion);

  auto mutable_map = message.mutable_keyvals();
  KeyValueMap::const_iterator it;
  for (it = keyvals.begin(); it != keyvals.end(); ++it) {
    (*mutable_map)[it->first] = it->second;
  }

  brillo::Blob content(message.ByteSizeLong());
  message.SerializeWithCachedSizesToArray(content.data());

  brillo::Blob digest_blob = CryptoLib::Sha256(content);
  std::string digest(digest_blob.begin(), digest_blob.end());

  // It is hard to make this atomic. In the case the file digest
  // and NVRAM space content are inconsistent, the file is deleted and NVRAM
  // space is updated on write.
  if (!brillo::WriteBlobToFileAtomic(boot_lockbox_filepath_, content, 0600)) {
    LOG(ERROR) << "Failed to write to boot lockbox file";
    return false;
  }
  // Update tpm_nvram.
  if (!tpm_nvspace_utility_->WriteNVSpace(digest)) {
    LOG(ERROR) << "Failed to write boot lockbox NVRAM space";
    return false;
  }

  brillo::SyncFileOrDirectory(boot_lockbox_filepath_, false /* is directory */,
                              true /* data sync */);
  // Update in memory information.
  key_value_store_ = keyvals;
  root_digest_ = digest;
  nvspace_state_ = NVSpaceState::kNVSpaceNormal;
  return true;
}

NVSpaceState NVRamBootLockbox::GetState() {
  return nvspace_state_;
}

void NVRamBootLockbox::SetState(const NVSpaceState state) {
  nvspace_state_ = state;
}

}  // namespace cryptohome
