// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_new_impl.h"

#include <string>

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

}  // namespace cryptohome
