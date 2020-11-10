// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM1_IMPL_H_
#define CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM1_IMPL_H_

#include <stdint.h>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <base/macros.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/rsa.h>

#include "cryptohome/signature_sealing_backend.h"

namespace cryptohome {

class TpmImpl;

// Implementation of signature-sealing operations for TPM 1.2. Based on the
// Certified Migratable Key functionality, with the CMK's private key contents
// playing the role of the sealed secret. The CMK is of 2048-bit size.
//
// Only the |kRsassaPkcs1V15Sha1| algorithm is supported by this implementation.
class SignatureSealingBackendTpm1Impl final : public SignatureSealingBackend {
 public:
  explicit SignatureSealingBackendTpm1Impl(TpmImpl* tpm);
  SignatureSealingBackendTpm1Impl(const SignatureSealingBackendTpm1Impl&) =
      delete;
  SignatureSealingBackendTpm1Impl& operator=(
      const SignatureSealingBackendTpm1Impl&) = delete;

  ~SignatureSealingBackendTpm1Impl() override;

  // SignatureSealingBackend:
  bool CreateSealedSecret(
      const brillo::Blob& public_key_spki_der,
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret,
      brillo::SecureBlob* secret_value,
      SignatureSealedData* sealed_secret_data) override;
  std::unique_ptr<UnsealingSession> CreateUnsealingSession(
      const SignatureSealedData& sealed_secret_data,
      const brillo::Blob& public_key_spki_der,
      const std::vector<ChallengeSignatureAlgorithm>& key_algorithms,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override;

 private:
  // Unowned.
  TpmImpl* const tpm_;
};

// Extracts the CMK's private key from the output of the migration procedure:
// the TPM_KEY12 blob of the migrated CMK in |migrated_cmk_key12_blob|, and the
// migration random XOR-mask in |migration_random_blob|. Returns the OpenSSL
// private key object.
crypto::ScopedRSA ExtractCmkPrivateKeyFromMigratedBlob(
    const brillo::Blob& migrated_cmk_key12_blob,
    const brillo::Blob& migration_random_blob,
    const brillo::Blob& cmk_pubkey,
    const brillo::Blob& cmk_pubkey_digest,
    const brillo::Blob& msa_composite_digest,
    RSA* migration_destination_rsa);

}  // namespace cryptohome

#endif  // CRYPTOHOME_SIGNATURE_SEALING_BACKEND_TPM1_IMPL_H_
