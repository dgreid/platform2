// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_new_impl.h"

#include <string>
#include <vector>

#include <tpm_manager-client/tpm_manager/dbus-constants.h>

namespace cryptohome {

namespace {
std::string OwnerDependencyEnumClassToString(
    TpmPersistentState::TpmOwnerDependency dependency) {
  switch (dependency) {
    case TpmPersistentState::TpmOwnerDependency::kInstallAttributes:
      return tpm_manager::kTpmOwnerDependency_Nvram;
    case TpmPersistentState::TpmOwnerDependency::kAttestation:
      return tpm_manager::kTpmOwnerDependency_Attestation;
    default:
      NOTREACHED() << __func__ << ": Unexpected enum class value: "
                   << static_cast<int>(dependency);
      return "";
  }
}

}  // namespace

TpmNewImpl::TpmNewImpl(tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility) {}

bool TpmNewImpl::GetOwnerPassword(brillo::SecureBlob* owner_password) {
  if (IsOwned()) {
    *owner_password =
        brillo::SecureBlob(last_tpm_manager_data_.owner_password());
    if (owner_password->empty()) {
      LOG(WARNING) << __func__
                   << ": Trying to get owner password after it is cleared.";
    }
  } else {
    LOG(ERROR)
        << __func__
        << ": Cannot get owner password until TPM is confirmed to be owned.";
    owner_password->clear();
  }
  return !owner_password->empty();
}

bool TpmNewImpl::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
    }
  }
  return tpm_manager_utility_ && tpm_manager_utility_->Initialize();
}

bool TpmNewImpl::CacheTpmManagerStatus() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetTpmStatus(&is_enabled_, &is_owned_,
                                            &last_tpm_manager_data_);
}

bool TpmNewImpl::UpdateLocalDataFromTpmManager() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }

  bool is_successful = false;
  bool has_received = false;

  // Repeats data copy into |last_tpm_manager_data_|; reasonable trade-off due
  // to low ROI to avoid that.
  bool is_connected = tpm_manager_utility_->GetOwnershipTakenSignalStatus(
      &is_successful, &has_received, &last_tpm_manager_data_);

  // When we need explicitly query tpm status either because the signal is not
  // ready for any reason, or because the signal is not received yet so we need
  // to run it once in case the signal is sent by tpm_manager before already.
  if (!is_connected || !is_successful ||
      (!has_received && shall_cache_tpm_manager_status_)) {
    // Retains |shall_cache_tpm_manager_status_| to be |true| if the signal
    // cannot be relied on (yet). Actually |!is_successful| suffices to update
    // |shall_cache_tpm_manager_status_|; by design, uses the redundancy just to
    // avoid confusion.
    shall_cache_tpm_manager_status_ &= (!is_connected || !is_successful);
    return CacheTpmManagerStatus();
  } else if (has_received) {
    is_enabled_ = true;
    is_owned_ = true;
  }
  return true;
}

bool TpmNewImpl::IsEnabled() {
  if (!is_enabled_) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__
                 << ": Failed to update TPM status from tpm manager.";
      return false;
    }
  }
  return is_enabled_;
}

bool TpmNewImpl::IsOwned() {
  if (!is_owned_) {
    if (!UpdateLocalDataFromTpmManager()) {
      LOG(ERROR) << __func__
                 << ": Failed to call |UpdateLocalDataFromTpmManager|.";
      return false;
    }
  }
  return is_owned_;
}

bool TpmNewImpl::IsOwnerPasswordPresent() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  bool is_owner_password_present = false;
  if (!tpm_manager_utility_->GetTpmNonsensitiveStatus(
          nullptr, nullptr, &is_owner_password_present, nullptr)) {
    LOG(ERROR) << __func__ << ": Failed to get |is_owner_password_present|.";
    return false;
  }
  return is_owner_password_present;
}

bool TpmNewImpl::HasResetLockPermissions() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  bool has_reset_lock_permissions = false;
  if (!tpm_manager_utility_->GetTpmNonsensitiveStatus(
          nullptr, nullptr, nullptr, &has_reset_lock_permissions)) {
    LOG(ERROR) << __func__ << ": Failed to get |has_reset_lock_permissions|.";
    return false;
  }
  return has_reset_lock_permissions;
}

bool TpmNewImpl::TakeOwnership(int, const brillo::SecureBlob&) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  if (IsOwned()) {
    LOG(INFO) << __func__ << ": TPM is already owned.";
    return true;
  }
  return tpm_manager_utility_->TakeOwnership();
}

void TpmNewImpl::SetOwnerPassword(const brillo::SecureBlob&) {
  LOG(WARNING) << __func__ << ": no-ops.";
}

void TpmNewImpl::SetIsEnabled(bool) {
  LOG(WARNING) << __func__ << ": no-ops.";
}

void TpmNewImpl::SetIsOwned(bool) {
  LOG(WARNING) << __func__ << ": no-ops.";
}

bool TpmNewImpl::GetDelegate(brillo::Blob* blob,
                             brillo::Blob* secret,
                             bool* has_reset_lock_permissions) {
  blob->clear();
  secret->clear();
  if (last_tpm_manager_data_.owner_delegate().blob().empty() ||
      last_tpm_manager_data_.owner_delegate().secret().empty()) {
    if (!CacheTpmManagerStatus()) {
      LOG(ERROR) << __func__
                 << ": Failed to call |UpdateLocalDataFromTpmManager|.";
      return false;
    }
  }
  const auto& owner_delegate = last_tpm_manager_data_.owner_delegate();
  *blob = brillo::BlobFromString(owner_delegate.blob());
  *secret = brillo::BlobFromString(owner_delegate.secret());
  *has_reset_lock_permissions = owner_delegate.has_reset_lock_permissions();
  return !blob->empty() && !secret->empty();
}

bool TpmNewImpl::DoesUseTpmManager() {
  return true;
}

bool TpmNewImpl::GetDictionaryAttackInfo(int* counter,
                                         int* threshold,
                                         bool* lockout,
                                         int* seconds_remaining) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->GetDictionaryAttackInfo(
      counter, threshold, lockout, seconds_remaining);
}

bool TpmNewImpl::ResetDictionaryAttackMitigation(const brillo::Blob&,
                                                 const brillo::Blob&) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ResetDictionaryAttackLock();
}

bool TpmNewImpl::RemoveOwnerDependency(
    TpmPersistentState::TpmOwnerDependency dependency) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->RemoveOwnerDependency(
      OwnerDependencyEnumClassToString(dependency));
}

bool TpmNewImpl::ClearStoredPassword() {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->ClearStoredOwnerPassword();
}

bool TpmNewImpl::GetVersionInfo(TpmVersionInfo* version_info) {
  if (!version_info) {
    LOG(ERROR) << __func__ << "version_info is not initialized.";
    return false;
  }

  // Version info on a device never changes. Returns from cache directly if we
  // have the cache.
  if (version_info_) {
    *version_info = *version_info_;
    return true;
  }

  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": failed to initialize |TpmManagerUtility|.";
    return false;
  }

  if (!tpm_manager_utility_->GetVersionInfo(
          &version_info->family, &version_info->spec_level,
          &version_info->manufacturer, &version_info->tpm_model,
          &version_info->firmware_version, &version_info->vendor_specific)) {
    LOG(ERROR) << __func__ << ": failed to get version info from tpm_manager.";
    return false;
  }

  version_info_ = *version_info;
  return true;
}

bool TpmNewImpl::SetDelegateDataFromTpmManager() {
  if (has_set_delegate_data_) {
    return true;
  }
  brillo::Blob blob, unused_secret;
  bool has_reset_lock_permissions = false;
  if (GetDelegate(&blob, &unused_secret, &has_reset_lock_permissions)) {
    // Don't log the error at this level but by the called function and the
    // functions that call it.
    has_set_delegate_data_ |= SetDelegateData(blob, has_reset_lock_permissions);
  }
  return has_set_delegate_data_;
}

base::Optional<bool> TpmNewImpl::IsDelegateBoundToPcr() {
  if (!SetDelegateDataFromTpmManager()) {
    LOG(WARNING) << __func__
                 << ": failed to call |SetDelegateDataFromTpmManager|.";
  }
  return TpmImpl::IsDelegateBoundToPcr();
}

bool TpmNewImpl::DelegateCanResetDACounter() {
  if (!SetDelegateDataFromTpmManager()) {
    LOG(WARNING) << __func__
                 << ": failed to call |SetDelegateDataFromTpmManager|.";
  }
  return TpmImpl::DelegateCanResetDACounter();
}

bool TpmNewImpl::DefineNvram(uint32_t index, size_t length, uint32_t flags) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  const bool write_define = flags & Tpm::kTpmNvramWriteDefine;
  const bool bind_to_pcr0 = flags & Tpm::kTpmNvramBindToPCR0;
  const bool firmware_readable = flags & Tpm::kTpmNvramFirmwareReadable;

  return tpm_manager_utility_->DefineSpace(index, length, write_define,
                                           bind_to_pcr0, firmware_readable);
}

bool TpmNewImpl::DestroyNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->DestroySpace(index);
}

bool TpmNewImpl::WriteNvram(uint32_t index, const brillo::SecureBlob& blob) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  tpm_manager::WriteSpaceRequest request;
  request.set_index(index);
  request.set_data(blob.to_string());
  return tpm_manager_utility_->WriteSpace(index, blob.to_string(), false);
}

bool TpmNewImpl::ReadNvram(uint32_t index, brillo::SecureBlob* blob) {
  if (!InitializeTpmManagerUtility()) {
    return false;
  }

  std::string output;
  const bool result = tpm_manager_utility_->ReadSpace(index, false, &output);
  brillo::SecureBlob tmp(output);
  blob->swap(tmp);
  return result;
}

bool TpmNewImpl::IsNvramDefined(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  std::vector<uint32_t> spaces;
  if (!tpm_manager_utility_->ListSpaces(&spaces)) {
    return false;
  }
  for (uint32_t space : spaces) {
    if (index == space) {
      return true;
    }
  }
  return false;
}

bool TpmNewImpl::IsNvramLocked(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked)) {
    return false;
  }
  return is_write_locked;
}

bool TpmNewImpl::WriteLockNvram(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  return tpm_manager_utility_->LockSpace(index);
}

unsigned int TpmNewImpl::GetNvramSize(uint32_t index) {
  if (!InitializeTpmManagerUtility()) {
    LOG(ERROR) << __func__ << ": Failed to initialize |TpmManagerUtility|.";
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  if (!tpm_manager_utility_->GetSpaceInfo(index, &size, &is_read_locked,
                                          &is_write_locked)) {
    return 0;
  }
  return size;
}

}  // namespace cryptohome
