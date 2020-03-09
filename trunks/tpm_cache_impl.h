// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_TPM_CACHE_IMPL_H_
#define TRUNKS_TPM_CACHE_IMPL_H_

#include "trunks/tpm_cache.h"

#include <base/macros.h>
#include <base/optional.h>

#include "trunks/tpm_generated.h"
#include "trunks/trunks_export.h"

namespace trunks {

// Implementation of the interface TpmCache.
class TRUNKS_EXPORT TpmCacheImpl : public TpmCache {
 public:
  explicit TpmCacheImpl(Tpm* const tpm);
  ~TpmCacheImpl() override = default;

  TPM_RC GetSaltingKeyPublicArea(TPMT_PUBLIC* public_area) override;

 private:
  // Salting key public area cache.
  base::Optional<TPMT_PUBLIC> salting_key_pub_area_;

  Tpm* const tpm_;

  DISALLOW_COPY_AND_ASSIGN(TpmCacheImpl);
};

}  // namespace trunks

#endif  // TRUNKS_TPM_CACHE_IMPL_H_
