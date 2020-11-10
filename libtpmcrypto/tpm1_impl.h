// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTPMCRYPTO_TPM1_IMPL_H_
#define LIBTPMCRYPTO_TPM1_IMPL_H_

#include "libtpmcrypto/tpm.h"

#include <string>

#include <trousers/tss.h>

namespace brillo {
class SecureBlob;
}  // namespace brillo

namespace tpmcrypto {

class Tpm1Impl : public Tpm {
 public:
  Tpm1Impl();
  Tpm1Impl(const Tpm1Impl&) = delete;
  Tpm1Impl& operator=(const Tpm1Impl&) = delete;

  ~Tpm1Impl() override;

  bool SealToPCR0(const brillo::SecureBlob& value,
                  brillo::SecureBlob* sealed_value) override;

  bool Unseal(const brillo::SecureBlob& sealed_value,
              brillo::SecureBlob* value) override;

  bool GetNVAttributes(uint32_t index, uint32_t* attributes) override;
  bool NVReadNoAuth(uint32_t index,
                    uint32_t offset,
                    size_t size,
                    std::string* data) override;

 private:
  // Tries to connect to the TPM
  TSS_HCONTEXT ConnectContext();

  // Connects to the TPM and return its context at |context_handle|.
  bool OpenAndConnectTpm(TSS_HCONTEXT* context_handle, TSS_RESULT* result);

  // Gets a handle to the TPM from the specified context
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS.
  bool ConnectContextAsUser(TSS_HCONTEXT* context_handle, TSS_HTPM* tpm_handle);

  // Gets a handle to the SRK.
  bool LoadSrk(TSS_HCONTEXT context_handle,
               TSS_HKEY* srk_handle,
               TSS_RESULT* result) const;
};

}  // namespace tpmcrypto

#endif  // LIBTPMCRYPTO_TPM1_IMPL_H_
