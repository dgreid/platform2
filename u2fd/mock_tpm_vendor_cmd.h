// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_MOCK_TPM_VENDOR_CMD_H_
#define U2FD_MOCK_TPM_VENDOR_CMD_H_

#include "u2fd/tpm_vendor_cmd.h"

#include <string>

#include <gmock/gmock.h>
#include <trunks/cr50_headers/u2f.h>

namespace u2f {

class MockTpmVendorCommandProxy : public TpmVendorCommandProxy {
 public:
  MOCK_METHOD(uint32_t,
              SendU2fGenerate,
              (const struct u2f_generate_req&, struct u2f_generate_resp*),
              (override));
  MOCK_METHOD(uint32_t,
              SendU2fSign,
              (const struct u2f_sign_req&, struct u2f_sign_resp*),
              (override));
  MOCK_METHOD(uint32_t,
              SendU2fAttest,
              (const struct u2f_attest_req&, struct u2f_attest_resp*),
              (override));
  MOCK_METHOD(uint32_t, GetG2fCertificate, (std::string*), (override));
};

}  // namespace u2f

#endif  // U2FD_MOCK_TPM_VENDOR_CMD_H_
