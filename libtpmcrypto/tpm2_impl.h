// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBTPMCRYPTO_TPM2_IMPL_H_
#define LIBTPMCRYPTO_TPM2_IMPL_H_

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/threading/platform_thread.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

#include "libtpmcrypto/tpm.h"

namespace brillo {
class SecureBlob;
}  // namespace brillo

namespace tpmcrypto {

class Tpm2Impl : public Tpm {
 public:
  Tpm2Impl();
  Tpm2Impl(const Tpm2Impl&) = delete;
  Tpm2Impl& operator=(const Tpm2Impl&) = delete;

  ~Tpm2Impl() override;

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
  // If already initialized this returns true, otherwise attempts to
  // initialize and returns whether initialization was successful.
  bool EnsureInitialized() WARN_UNUSED_RESULT;
  bool CreatePcr0PolicyDigest(std::string* policy_digest);
  bool CreateHmacSession(std::unique_ptr<trunks::HmacSession>* hmac_session);
  bool CreatePolicySessionForPCR0(
      std::unique_ptr<trunks::PolicySession>* policy_session);
  bool SealData(trunks::AuthorizationDelegate* session_delegate,
                const std::string& policy_digest,
                const brillo::SecureBlob& value,
                brillo::SecureBlob* sealed_value);
  bool UnsealData(trunks::AuthorizationDelegate* policy_delegate,
                  const brillo::SecureBlob& sealed_value,
                  brillo::SecureBlob* value);

  bool is_initialized_ = false;
  std::unique_ptr<trunks::TrunksFactoryImpl> factory_impl_;
  std::unique_ptr<trunks::TpmUtility> tpm_utility_;
};

}  // namespace tpmcrypto

#endif  // LIBTPMCRYPTO_TPM2_IMPL_H_
