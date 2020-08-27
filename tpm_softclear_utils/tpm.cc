// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_softclear_utils/tpm.h"

#if USE_TPM2
#include "tpm_softclear_utils/tpm2_impl.h"
#else
#include "tpm_softclear_utils/tpm_impl.h"
#endif

namespace tpm_softclear_utils {

Tpm* Tpm::Create() {
#if USE_TPM2
  return new Tpm2Impl();
#else
  return new TpmImpl();
#endif
}

}  // namespace tpm_softclear_utils
