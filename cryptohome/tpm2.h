// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM2_H_
#define CRYPTOHOME_TPM2_H_

#include <trunks/error_codes.h>
#include <trunks/tpm_utility.h>

namespace cryptohome {
const char kTpmWellKnownPassword[] = ""; /* not used */
typedef trunks::TPM_RC TpmReturnCode;
typedef trunks::TpmUtility::AsymmetricKeyUsage AsymmetricKeyUsage;
}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM2_H_
