// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_pump_type.h>
#include <trunks/error_codes.h>
#include <trunks/password_authorization_delegate.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_utility.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"

namespace {
constexpr base::TimeDelta kDefaultTimeout = base::TimeDelta::FromMinutes(2);
}  // namespace

using tpm_manager::NvramResult;

namespace cryptohome {

NVSpaceState MapTpmRc(trunks::TPM_RC rc) {
  switch (rc) {
    case trunks::TPM_RC_SUCCESS:
      return NVSpaceState::kNVSpaceNormal;
    case trunks::TPM_RC_HANDLE:
      return NVSpaceState::kNVSpaceUndefined;
    case trunks::TPM_RC_NV_UNINITIALIZED:
      return NVSpaceState::kNVSpaceUninitialized;
    case trunks::TPM_RC_NV_LOCKED:
      return NVSpaceState::kNVSpaceWriteLocked;
    default:
      return NVSpaceState::kNVSpaceError;
  }
}

std::string NvramResult2Str(NvramResult r) {
  switch (r) {
    case NvramResult::NVRAM_RESULT_SUCCESS:
      return "NVRAM_RESULT_SUCCESS";
    case NvramResult::NVRAM_RESULT_DEVICE_ERROR:
      return "NVRAM_RESULT_DEVICE_ERROR";
    case NvramResult::NVRAM_RESULT_ACCESS_DENIED:
      return "NVRAM_RESULT_ACCESS_DENIED";
    case NvramResult::NVRAM_RESULT_INVALID_PARAMETER:
      return "NVRAM_RESULT_INVALID_PARAMETER";
    case NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST:
      return "NVRAM_RESULT_SPACE_DOES_NOT_EXIST";
    case NvramResult::NVRAM_RESULT_SPACE_ALREADY_EXISTS:
      return "NVRAM_RESULT_SPACE_ALREADY_EXISTS";
    case NvramResult::NVRAM_RESULT_OPERATION_DISABLED:
      return "NVRAM_RESULT_OPERATION_DISABLED";
    case NvramResult::NVRAM_RESULT_INSUFFICIENT_SPACE:
      return "NVRAM_RESULT_INSUFFICIENT_SPACE";
    case NvramResult::NVRAM_RESULT_IPC_ERROR:
      return "NVRAM_RESULT_IPC_ERROR";
  }
}

TPM2NVSpaceUtility::TPM2NVSpaceUtility(
    org::chromium::TpmNvramProxyInterface* tpm_nvram,
    trunks::TrunksFactory* trunks_factory) {
  tpm_nvram_ = tpm_nvram;
  trunks_factory_ = trunks_factory;
}

bool TPM2NVSpaceUtility::Initialize() {
  if (!tpm_nvram_) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = base::MakeRefCounted<dbus::Bus>(options);
    CHECK(bus_->Connect()) << "Failed to connect to system D-Bus";
    default_tpm_nvram_ = std::make_unique<org::chromium::TpmNvramProxy>(bus_);
    tpm_nvram_ = default_tpm_nvram_.get();
  }
  if (!trunks_factory_) {
    default_trunks_factory_ = std::make_unique<trunks::TrunksFactoryImpl>();
    if (!default_trunks_factory_->Initialize()) {
      LOG(ERROR) << "Failed to initialize trunks factory";
      return false;
    }
    trunks_factory_ = default_trunks_factory_.get();
  }
  return true;
}

bool TPM2NVSpaceUtility::DefineNVSpace() {
  tpm_manager::DefineSpaceRequest request;
  request.set_index(kBootLockboxNVRamIndex);
  request.set_size(kNVSpaceSize);
  request.add_attributes(tpm_manager::NVRAM_READ_AUTHORIZATION);
  request.add_attributes(tpm_manager::NVRAM_BOOT_WRITE_LOCK);
  request.add_attributes(tpm_manager::NVRAM_WRITE_AUTHORIZATION);
  request.set_authorization_value(kWellKnownPassword);

  tpm_manager::DefineSpaceReply reply;
  brillo::ErrorPtr error;
  if (!tpm_nvram_->DefineSpace(request, &reply, &error,
                               kDefaultTimeout.InMilliseconds())) {
    LOG(ERROR) << "Failed to call DefineSpace: " << error->GetMessage();
    return false;
  }
  if (reply.result() != tpm_manager::NVRAM_RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to define nvram space: "
               << NvramResult2Str(reply.result());
    return false;
  }
  // TODO(xzhou): notify tpm_managerd ready to drop key.
  return true;
}

bool TPM2NVSpaceUtility::DefineNVSpaceBeforeOwned() {
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPMA_NV attributes = trunks::TPMA_NV_WRITE_STCLEAR |
                               trunks::TPMA_NV_AUTHREAD |
                               trunks::TPMA_NV_AUTHWRITE;
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->DefineNVSpace(
          kBootLockboxNVRamIndex, kNVSpaceSize, attributes, kWellKnownPassword,
          "", pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error define nv space, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2NVSpaceUtility::WriteNVSpace(const std::string& digest) {
  if (digest.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "Wrong digest size, expected: " << SHA256_DIGEST_LENGTH
               << " got: " << digest.size();
    return false;
  }

  BootLockboxNVSpace BootLockboxNVSpace;
  BootLockboxNVSpace.version = kNVSpaceVersion;
  BootLockboxNVSpace.flags = 0;
  memcpy(BootLockboxNVSpace.digest, digest.data(), SHA256_DIGEST_LENGTH);
  std::string nvram_data(reinterpret_cast<const char*>(&BootLockboxNVSpace),
                         kNVSpaceSize);
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->WriteNVSpace(
          kBootLockboxNVRamIndex, 0 /* offset */, nvram_data,
          false /* using_owner_authorization */, false /* extend */,
          pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error writing nvram space, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

bool TPM2NVSpaceUtility::ReadNVSpace(std::string* digest,
                                     NVSpaceState* result) {
  *result = NVSpaceState::kNVSpaceError;
  std::string nvram_data;
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC rc =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->ReadNVSpace(
          kBootLockboxNVRamIndex, 0 /* offset */, kNVSpaceSize,
          false /* using owner authorization */, &nvram_data, pw_auth.get()));
  if (rc != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading nvram space, error: "
               << trunks::GetErrorString(rc);
    *result = MapTpmRc(rc);
    return false;
  }
  if (nvram_data.size() != kNVSpaceSize) {
    LOG(ERROR) << "Error reading nvram space, invalid data length, expected:"
               << kNVSpaceSize << ", got " << nvram_data.size();
    return false;
  }
  BootLockboxNVSpace BootLockboxNVSpace;
  memcpy(&BootLockboxNVSpace, nvram_data.data(), kNVSpaceSize);
  if (BootLockboxNVSpace.version != kNVSpaceVersion) {
    LOG(ERROR) << "Error reading nvram space, invalid version";
    return false;
  }
  digest->assign(reinterpret_cast<const char*>(BootLockboxNVSpace.digest),
                 SHA256_DIGEST_LENGTH);
  *result = NVSpaceState::kNVSpaceNormal;
  return true;
}

bool TPM2NVSpaceUtility::LockNVSpace() {
  auto pw_auth = trunks_factory_->GetPasswordAuthorization(kWellKnownPassword);
  trunks::TPM_RC result =
      trunks::GetFormatOneError(trunks_factory_->GetTpmUtility()->LockNVSpace(
          kBootLockboxNVRamIndex, false /* lock read */, true /* lock write */,
          false /* using owner authorization */, pw_auth.get()));
  if (result != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error locking nvspace, error: "
               << trunks::GetErrorString(result);
    return false;
  }
  return true;
}

}  // namespace cryptohome
