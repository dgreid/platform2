// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation/common/tpm_utility_common.h"

#include <memory>
#include <vector>

#include <base/logging.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

namespace attestation {

TpmUtilityCommon::TpmUtilityCommon()
    : tpm_manager_utility_(new tpm_manager::TpmManagerUtility()) {}

TpmUtilityCommon::TpmUtilityCommon(
    tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility) {}

TpmUtilityCommon::~TpmUtilityCommon() {}

bool TpmUtilityCommon::Initialize() {
  return tpm_manager_utility_->Initialize();
}

bool TpmUtilityCommon::IsTpmReady() {
  if (!is_ready_) {
    CacheTpmState();
  }
  return is_ready_;
}

bool TpmUtilityCommon::GetEndorsementPassword(std::string* password) {
  if (endorsement_password_.empty()) {
    if (!CacheTpmState()) {
      return false;
    }
    if (endorsement_password_.empty()) {
      LOG(WARNING) << ": TPM endorsement password is not available.";
      return false;
    }
  }
  *password = endorsement_password_;
  return true;
}

bool TpmUtilityCommon::GetOwnerPassword(std::string* password) {
  if (owner_password_.empty()) {
    if (!CacheTpmState()) {
      return false;
    }
    if (owner_password_.empty()) {
      LOG(WARNING) << ": TPM owner password is not available.";
      return false;
    }
  }
  *password = owner_password_;
  return true;
}

bool TpmUtilityCommon::CacheTpmState() {
  tpm_manager::LocalData local_data;
  bool is_enabled{false};
  bool is_owned{false};
  if (!tpm_manager_utility_->GetTpmStatus(&is_enabled, &is_owned,
                                          &local_data)) {
    LOG(ERROR) << __func__ << ": Failed to get tpm status from tpm_manager.";
    return false;
  }
  is_ready_ = is_enabled && is_owned;
  endorsement_password_ = local_data.endorsement_password();
  owner_password_ = local_data.owner_password();
  delegate_blob_ = local_data.owner_delegate().blob();
  delegate_secret_ = local_data.owner_delegate().secret();
  return true;
}

bool TpmUtilityCommon::RemoveOwnerDependency() {
  return tpm_manager_utility_->RemoveOwnerDependency(
      tpm_manager::kTpmOwnerDependency_Attestation);
}

}  // namespace attestation
