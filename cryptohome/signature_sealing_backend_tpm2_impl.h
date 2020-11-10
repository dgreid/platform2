// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <base/macros.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class Tpm2Impl;

// Implementation of signature-sealing operations for TPM 2.0. Based on the
// TPM2_PolicySigned functionality.
//
// Notes on implementation:
// * |delegate_blob| and |delegate_secret| parameters are ignored;
// * Size of the |pcr_restrictions| parameter for CreateSealedSecret() should
//   not exceed 8.
class SignatureSealingBackendTpm2Impl final : public SignatureSealingBackend {
 public:
  explicit SignatureSealingBackendTpm2Impl(Tpm2Impl* tpm);
  SignatureSealingBackendTpm2Impl(const SignatureSealingBackendTpm2Impl&) =
      delete;
  SignatureSealingBackendTpm2Impl& operator=(
      const SignatureSealingBackendTpm2Impl&) = delete;

  ~SignatureSealingBackendTpm2Impl() override;

  // SignatureSealingBackend:
  bool CreateSealedSecret(
      const brillo::Blob& public_key_spki_der,
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
      const brillo::Blob& /* delegate_blob */,
      const brillo::Blob& /* delegate_secret */,
      brillo::SecureBlob* secret_value,
      SignatureSealedData* sealed_secret_data) override;
  std::unique_ptr<UnsealingSession> CreateUnsealingSession(
      const SignatureSealedData& sealed_secret_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      const brillo::Blob& /* delegate_blob */,
      const brillo::Blob& /* delegate_secret */) override;

 private:
  // Unowned.
  Tpm2Impl* const tpm_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM2_IMPL_H_
