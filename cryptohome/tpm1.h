// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM1_H_
#define CRYPTOHOME_TPM1_H_

#include <trousers/tss.h>

namespace cryptohome {
const char kTpmWellKnownPassword[] = TSS_WELL_KNOWN_SECRET;
typedef TSS_RESULT TpmReturnCode;
// Specifies what the key can be used for.
enum AsymmetricKeyUsage { kDecryptKey, kSignKey, kDecryptAndSignKey };
}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM1_H_
