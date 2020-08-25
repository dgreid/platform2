// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/mock_tpm_utility.h"

namespace trunks {

MockTpmUtility::MockTpmUtility() {}
MockTpmUtility::~MockTpmUtility() {}

TPM_RC MockTpmUtility::PinWeaverInsertLeaf(
    uint8_t protocol_version,
    uint64_t label,
    const std::string& h_aux,
    const brillo::SecureBlob& le_secret,
    const brillo::SecureBlob& he_secret,
    const brillo::SecureBlob& reset_secret,
    const std::map<uint32_t, uint32_t>& delay_schedule,
    const ValidPcrCriteria& valid_pcr_criteria,
    uint32_t* result_code,
    std::string* root_hash,
    std::string* cred_metadata,
    std::string* mac) {
  return TPM_RC_SUCCESS;
}

TPM_RC MockTpmUtility::PinWeaverTryAuth(uint8_t protocol_version,
                                        const brillo::SecureBlob& le_secret,
                                        const std::string& h_aux,
                                        const std::string& cred_metadata,
                                        uint32_t* result_code,
                                        std::string* root_hash,
                                        uint32_t* seconds_to_wait,
                                        brillo::SecureBlob* he_secret,
                                        brillo::SecureBlob* reset_secret,
                                        std::string* cred_metadata_out,
                                        std::string* mac_out) {
  return TPM_RC_SUCCESS;
}

}  // namespace trunks
