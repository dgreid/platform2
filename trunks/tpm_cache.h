// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_CACHE_H_
#define TRUNKS_TPM_CACHE_H_

#include <base/macros.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// TpmCache is an interface which provides access to TPM cache information.
class TRUNKS_EXPORT TpmCache {
 public:
  TpmCache() = default;
  virtual ~TpmCache() = default;

  // Stores the cached salting key public area in |public_area|. If the cache
  // doesn't exist, gets the public area from TPM and caches it. |public_area|
  // is untouched if there's an error.
  virtual TPM_RC GetSaltingKeyPublicArea(TPMT_PUBLIC* public_area) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(TpmCache);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_CACHE_H_
