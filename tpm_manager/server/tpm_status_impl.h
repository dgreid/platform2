// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_SERVER_TPM_STATUS_IMPL_H_
#define TPM_MANAGER_SERVER_TPM_STATUS_IMPL_H_

#include "tpm_manager/server/tpm_status.h"

#include <memory>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>
#include <trousers/tss.h>

#include <tpm_manager/server/tpm_connection.h>
#include "tpm_manager/common/typedefs.h"

namespace tpm_manager {

class TpmStatusImpl : public TpmStatus {
 public:
  TpmStatusImpl() = default;
  ~TpmStatusImpl() override = default;

  // TpmState methods.
  bool IsTpmEnabled() override;
  bool GetTpmOwned(TpmOwnershipStatus* status) override;
  bool GetDictionaryAttackInfo(uint32_t* counter,
                               uint32_t* threshold,
                               bool* lockout,
                               uint32_t* seconds_remaining) override;
  bool GetVersionInfo(uint32_t* family,
                      uint64_t* spec_level,
                      uint32_t* manufacturer,
                      uint32_t* tpm_model,
                      uint64_t* firmware_version,
                      std::vector<uint8_t>* vendor_specific) override;

  void MarkRandomOwnerPasswordSet() override;

 private:
  // Tests if the TPM owner password is the default one. Returns:
  // 1. true if the test succeed.
  // 2. false if authentication fails with the default owner password.
  // 3. base::nullopt if any other errors.
  //
  // Note that, w/o any useful cache data, testing tpm with owner auth means it
  // could increse DA counter or even fail during DA lockout. In case of no
  // useful delegate to reset DA, we don't have any way to reset DA so the all
  // the hwsec daemons cannot function correctly until DA unlocks itself after
  // timeout (crbug/1110741).
  base::Optional<bool> TestTpmWithDefaultOwnerPassword();
  // This method refreshes the |is_owned_| and |is_enabled_| status of the
  // Tpm. It can be called multiple times.
  void RefreshOwnedEnabledInfo();
  // This method wraps calls to Tspi_TPM_GetCapability. |data| is set to
  // the raw capability data. If the optional out argument |tpm_result| is
  // provided, it is set to the result of the |Tspi_TPM_GetCapability| call.
  bool GetCapability(uint32_t capability,
                     uint32_t sub_capability,
                     std::vector<uint8_t>* data,
                     TSS_RESULT* tpm_result);

  TpmConnection tpm_connection_;
  bool is_enabled_{false};

  // Whether the TPM ownership has been taken with the default owner password.
  // Note that a true value doesn't necessary mean the entire TPM initialization
  // process has finished.
  bool is_owned_{false};

  // Whether the TPM is fully initialized.
  TpmOwnershipStatus ownership_status_{kTpmUnowned};

  bool is_enable_initialized_{false};

  // Whether current owner password in the TPM is the default one; in case of
  // nullopt the password status is not determined yet.
  base::Optional<bool> is_owner_password_default_;

  DISALLOW_COPY_AND_ASSIGN(TpmStatusImpl);
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_SERVER_TPM_STATUS_IMPL_H_
