// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_cache_impl.h"

#include <base/logging.h>

#include "trunks/tpm_generated.h"
#include "trunks/tpm_utility.h"

namespace trunks {

TpmCacheImpl::TpmCacheImpl(Tpm* const tpm): tpm_(tpm) {}

TPM_RC TpmCacheImpl::GetSaltingKeyPublicArea(TPMT_PUBLIC* public_area) {
  // sanity check
  if (!public_area) {
    LOG(ERROR) << __func__ << ": public_area is uninitialized.";
    return TPM_RC_FAILURE;
  }

  if (salting_key_pub_area_) {
    // return from cache
    *public_area = *salting_key_pub_area_;
    return TPM_RC_SUCCESS;
  }

  TPM2B_NAME unused_out_name;
  TPM2B_NAME unused_qualified_name;
  TPM2B_PUBLIC public_data;
  TPM_RC result = tpm_->ReadPublicSync(
      kSaltingKey,
      "" /* object_handle_name, not used */,
      &public_data,
      &unused_out_name,
      &unused_qualified_name,
      nullptr /* authorization_delegate */);

  if (result == TPM_RC_SUCCESS) {
    salting_key_pub_area_ = public_data.public_area;
    *public_area = *salting_key_pub_area_;
  }

  return result;
}

}  // namespace trunks
