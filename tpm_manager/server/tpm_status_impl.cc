// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/server/tpm_status_impl.h"

#include <algorithm>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/optional.h>
#include <brillo/file_utils.h>
#include <tpm_manager/server/tpm_util.h>
#include <trousers/trousers.h>
#include <trousers/tss.h>

namespace {

// Minimum size of TPM_DA_INFO struct.
constexpr size_t kMinimumDaInfoSize = 21;

// Minimum size of TPM_CAP_VERSION_INFO struct.
constexpr size_t kMinimumVersionInfoSize = 17;

// The TPM manufacturer code of Infineon.
constexpr uint32_t kInfineonManufacturerCode = 0x49465800;

// The Infineon-specific DA info sub-capability flag.
constexpr uint32_t kInfineonMfrSubCapability = 0x00000802;

// The offset of DA counter in the Infineon-specific DA info data.
constexpr size_t kInfineonDACounterOffset = 9;

// The flag that tells if the tpm is full initialized.
constexpr char kTpmFullyInitializedPath[] =
    "/mnt/stateful_partition/.tpm_owned";

bool TouchTpmFullyInitializedPath() {
  return brillo::WriteBlobToFile<std::vector<char>>(
      base::FilePath(kTpmFullyInitializedPath), {});
}

}  // namespace

namespace tpm_manager {

bool TpmStatusImpl::IsTpmEnabled() {
  if (!is_enable_initialized_) {
    RefreshOwnedEnabledInfo();
  }
  return is_enabled_;
}

bool TpmStatusImpl::GetTpmOwned(TpmStatus::TpmOwnershipStatus* status) {
  CHECK(status);
  if (kTpmOwned == ownership_status_) {
    *status = ownership_status_;
    return true;
  }

  if (!is_owned_) {
    // update is_owned_
    RefreshOwnedEnabledInfo();
  }

  if (!is_owned_) {
    if (!base::DeleteFile(base::FilePath(kTpmFullyInitializedPath))) {
      LOG(WARNING) << __func__ << ": Failed to delete "
                   << kTpmFullyInitializedPath;
    }
    // We even haven't tried to take ownership yet.
    ownership_status_ = kTpmUnowned;
    *status = ownership_status_;
    return true;
  }

  const base::Optional<bool> is_default_owner_password =
      TestTpmWithDefaultOwnerPassword();
  if (!is_default_owner_password.has_value()) {
    LOG(ERROR) << __func__ << ": Failed to test default owner password.";
    return false;
  }
  ownership_status_ = *is_default_owner_password ? kTpmPreOwned : kTpmOwned;

  *status = ownership_status_;
  return true;
}

bool TpmStatusImpl::GetDictionaryAttackInfo(uint32_t* counter,
                                            uint32_t* threshold,
                                            bool* lockout,
                                            uint32_t* seconds_remaining) {
  CHECK(counter);
  CHECK(threshold);
  CHECK(lockout);
  CHECK(seconds_remaining);
  std::vector<uint8_t> capability_data;
  if (!GetCapability(TSS_TPMCAP_DA_LOGIC, TPM_ET_KEYHANDLE, &capability_data,
                     nullptr) ||
      capability_data.size() < kMinimumDaInfoSize) {
    LOG(ERROR) << "Error getting tpm capability data for DA info.";
    return false;
  }
  if (static_cast<uint16_t>(capability_data[1]) == TPM_TAG_DA_INFO) {
    TPM_DA_INFO da_info;
    uint64_t offset = 0;
    Trspi_UnloadBlob_DA_INFO(&offset, capability_data.data(), &da_info);
    *counter = da_info.currentCount;
    *threshold = da_info.thresholdCount;
    *lockout = (da_info.state == TPM_DA_STATE_ACTIVE);
    *seconds_remaining = da_info.actionDependValue;
  }

  // For Infineon, pulls the counter out of vendor-specific data and checks if
  // it matches the value in DA_INFO.

  if (!GetCapability(TSS_TPMCAP_PROPERTY, TSS_TPMCAP_PROP_MANUFACTURER,
                     &capability_data, nullptr) ||
      capability_data.size() != sizeof(uint32_t)) {
    LOG(WARNING) << "Failed to query TSS_TPMCAP_PROP_MANUFACTURER. "
                    "Using the DA info from TSS_TPMCAP_DA_LOGIC.";
    return true;
  }

  uint32_t manufacturer;
  uint64_t offset = 0;
  Trspi_UnloadBlob_UINT32(&offset, &manufacturer, capability_data.data());
  if (manufacturer != kInfineonManufacturerCode) {
    return true;
  }

  if (!GetCapability(TSS_TPMCAP_MFR, kInfineonMfrSubCapability,
                     &capability_data, nullptr)) {
    LOG(WARNING) << "Failed to query Infineon MFR capability. "
                    "Using the DA info from TSS_TPMCAP_DA_LOGIC.";
    return true;
  }

  if (capability_data.size() <= kInfineonDACounterOffset) {
    LOG(WARNING) << "Couldn't read DA counter from Infineon's MFR "
                    "capability. Using the DA info from TSS_TPMCAP_DA_LOGIC.";
    return true;
  }

  uint32_t vendor_da_counter =
      static_cast<uint32_t>(capability_data[kInfineonDACounterOffset]);
  if (*counter != vendor_da_counter) {
    LOG(WARNING) << "DA counter mismatch for Infineon: " << *counter << " vs. "
                 << vendor_da_counter << ". Using the larger one.";
    *counter = std::max(*counter, vendor_da_counter);
  }
  return true;
}

bool TpmStatusImpl::GetVersionInfo(uint32_t* family,
                                   uint64_t* spec_level,
                                   uint32_t* manufacturer,
                                   uint32_t* tpm_model,
                                   uint64_t* firmware_version,
                                   std::vector<uint8_t>* vendor_specific) {
  CHECK(family);
  CHECK(spec_level);
  CHECK(manufacturer);
  CHECK(tpm_model);
  CHECK(firmware_version);
  CHECK(vendor_specific);

  std::vector<uint8_t> capability_data;
  if (!GetCapability(TSS_TPMCAP_VERSION_VAL, 0, &capability_data, nullptr) ||
      capability_data.size() < kMinimumVersionInfoSize ||
      static_cast<uint16_t>(capability_data[1]) != TPM_TAG_CAP_VERSION_INFO) {
    LOG(ERROR) << "Error getting TPM version capability data.";
    return false;
  }

  TPM_CAP_VERSION_INFO tpm_version;
  uint64_t offset = 0;
  Trspi_UnloadBlob_CAP_VERSION_INFO(&offset, capability_data.data(),
                                    &tpm_version);
  *family = 0x312e3200;
  *spec_level = (static_cast<uint64_t>(tpm_version.specLevel) << 32) |
                tpm_version.errataRev;
  *manufacturer =
      (tpm_version.tpmVendorID[0] << 24) | (tpm_version.tpmVendorID[1] << 16) |
      (tpm_version.tpmVendorID[2] << 8) | (tpm_version.tpmVendorID[3] << 0);
  // There's no generic model field in the spec. Model information might be
  // present in the vendor-specific data returned by CAP_VERSION_INFO, so if
  // we ever require to know the model, we'll need to check with hardware
  // vendors for the best way to determine it.
  *tpm_model = ~0;
  *firmware_version =
      (tpm_version.version.revMajor << 8) | tpm_version.version.revMinor;
  const uint8_t* data =
      reinterpret_cast<const uint8_t*>(tpm_version.vendorSpecific);
  vendor_specific->assign(data, data + tpm_version.vendorSpecificSize);
  free(tpm_version.vendorSpecific);
  return true;
}

base::Optional<bool> TpmStatusImpl::TestTpmWithDefaultOwnerPassword() {
  if (base::PathExists(base::FilePath(kTpmFullyInitializedPath))) {
    is_owner_password_default_ = false;
  }

  if (is_owner_password_default_.has_value()) {
    return is_owner_password_default_;
  }

  TpmConnection connection(GetDefaultOwnerPassword());
  TSS_HTPM tpm_handle = connection.GetTpm();
  if (tpm_handle == 0) {
    return base::nullopt;
  }

  // Call Tspi_TPM_GetStatus to test the default owner password.
  TSS_BOOL current_status = false;
  TSS_RESULT result =
      Tspi_TPM_GetStatus(tpm_handle, TSS_TPMSTATUS_DISABLED, &current_status);

  if (result == TPM_SUCCESS) {
    is_owner_password_default_ = true;
  } else if (result == TPM_ERROR(TPM_E_AUTHFAIL)) {
    is_owner_password_default_ = false;
    if (!TouchTpmFullyInitializedPath()) {
      LOG(WARNING) << __func__ << ": Failed to touch "
                   << kTpmFullyInitializedPath;
    }
  } else {
    TPM_LOG(ERROR, result) << "Unexpected error calling |Tspi_TPM_GetStatus|.";
  }
  return is_owner_password_default_;
}

void TpmStatusImpl::RefreshOwnedEnabledInfo() {
  TSS_RESULT result;
  std::vector<uint8_t> capability_data;
  if (!GetCapability(TSS_TPMCAP_PROPERTY, TSS_TPMCAP_PROP_OWNER,
                     &capability_data, &result)) {
    if (ERROR_CODE(result) == TPM_E_DISABLED) {
      is_enable_initialized_ = true;
      is_enabled_ = false;
    }
  } else {
    is_enable_initialized_ = true;
    is_enabled_ = true;
    // |capability_data| should be populated with a TSS_BOOL which is true iff
    // the Tpm is owned.
    if (capability_data.size() != sizeof(TSS_BOOL)) {
      LOG(ERROR) << "Error refreshing Tpm ownership information.";
      return;
    }
    is_owned_ = (capability_data[0] != 0);
  }
}

bool TpmStatusImpl::GetCapability(uint32_t capability,
                                  uint32_t sub_capability,
                                  std::vector<uint8_t>* data,
                                  TSS_RESULT* tpm_result) {
  CHECK(data);
  TSS_HTPM tpm_handle = tpm_connection_.GetTpm();
  if (tpm_handle == 0) {
    if (tpm_result) {
      *tpm_result = TSS_E_COMM_FAILURE;
    }
    return false;
  }
  uint32_t length = 0;
  trousers::ScopedTssMemory buf(tpm_connection_.GetContext());
  TSS_RESULT result = Tspi_TPM_GetCapability(
      tpm_handle, capability, sizeof(uint32_t),
      reinterpret_cast<BYTE*>(&sub_capability), &length, buf.ptr());
  if (tpm_result) {
    *tpm_result = result;
  }
  if (TPM_ERROR(result)) {
    LOG(ERROR) << "Error getting TPM capability data.";
    return false;
  }
  data->assign(buf.value(), buf.value() + length);
  return true;
}

void TpmStatusImpl::MarkRandomOwnerPasswordSet() {
  // Also makes sure the state machine is consistent.
  is_enable_initialized_ = is_enabled_ = is_owned_ = true;
  ownership_status_ = kTpmOwned;
  is_owner_password_default_ = false;
  if (!TouchTpmFullyInitializedPath()) {
    LOG(WARNING) << __func__ << ": Failed to touch "
                 << kTpmFullyInitializedPath;
  }
}

}  // namespace tpm_manager
