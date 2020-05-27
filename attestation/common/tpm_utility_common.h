// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_COMMON_TPM_UTILITY_COMMON_H_
#define ATTESTATION_COMMON_TPM_UTILITY_COMMON_H_

#include "attestation/common/tpm_utility.h"

#include <memory>
#include <string>

#include <base/macros.h>
#include <tpm_manager/client/tpm_manager_utility.h>

namespace attestation {

// A TpmUtility implementation for version-independent functions.
class TpmUtilityCommon : public TpmUtility {
 public:
  TpmUtilityCommon();
  // Testing constructor.
  explicit TpmUtilityCommon(
      tpm_manager::TpmManagerUtility* tpm_manager_utility);
  ~TpmUtilityCommon() override;

  // TpmUtility methods.
  bool Initialize() override;
  bool IsTpmReady() override;
  bool RemoveOwnerDependency() override;

 protected:
  // Gets the endorsement password from tpm_managerd. Returns false if the
  // password is not available.
  bool GetEndorsementPassword(std::string* password);

  // Gets the owner password from tpm_managerd. Returns false if the password is
  // not available.
  bool GetOwnerPassword(std::string* password);

  // Caches various TPM state including owner / endorsement passwords. On
  // success, fields like is_ready_ and owner_password_ will be populated.
  // Returns true on success.
  bool CacheTpmState();

  bool is_ready_{false};
  std::string endorsement_password_;
  std::string owner_password_;
  std::string delegate_blob_;
  std::string delegate_secret_;

  std::unique_ptr<tpm_manager::TpmManagerUtility> tpm_manager_utility_;

  // For testing purpose.
  template <typename T>
  friend class TpmUtilityCommonTest;

  DISALLOW_COPY_AND_ASSIGN(TpmUtilityCommon);
};

}  // namespace attestation

#endif  // ATTESTATION_COMMON_TPM_UTILITY_COMMON_H_
