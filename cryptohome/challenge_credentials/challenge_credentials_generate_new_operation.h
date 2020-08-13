// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_GENERATE_NEW_OPERATION_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_GENERATE_NEW_OPERATION_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/secure_blob.h>

#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/signature_sealing_backend.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class Credentials;
class KeyChallengeService;
class Tpm;

// This operation generates new credentials for the given user and the
// referenced cryptographic key. This operation involves making challenge
// request(s) against the specified key.
//
// This class is not expected to be used directly by client code; instead,
// methods of ChallengeCredentialsHelper should be called.
class ChallengeCredentialsGenerateNewOperation final
    : public ChallengeCredentialsOperation {
 public:
  using KeysetSignatureChallengeInfo =
      SerializedVaultKeyset_SignatureChallengeInfo;

  // If the operation succeeds, |credentials| will contain the generated
  // credentials that can be used for encryption of the user's vault keyset,
  // with the challenge_credentials_keyset_info() field containing the data to
  // be stored in the created vault keyset.
  using CompletionCallback =
      base::OnceCallback<void(std::unique_ptr<Credentials> credentials)>;

  // |key_challenge_service| is a non-owned pointer which must outlive the
  // created instance.
  // |key_data| must have the |KEY_TYPE_CHALLENGE_RESPONSE| type.
  //
  // |pcr_restrictions| is the list of PCR sets; the created credentials will be
  // protected in a way that decrypting them back is possible iff at least one
  // of these sets is satisfied. Each PCR value set must be non-empty; pass
  // empty list of sets in order to have no PCR binding. The used
  // SignatureSealingBackend implementation may impose constraint on the maximum
  // allowed number of sets.
  //
  // The result is reported via |completion_callback|.
  ChallengeCredentialsGenerateNewOperation(
      KeyChallengeService* key_challenge_service,
      Tpm* tpm,
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret,
      const std::string& account_id,
      const KeyData& key_data,
      const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
      CompletionCallback completion_callback);

  ~ChallengeCredentialsGenerateNewOperation() override;

  // ChallengeCredentialsOperation:
  void Start() override;
  void Abort() override;

 private:
  // Starts the processing. Returns |false| on fatal error.
  bool StartProcessing();

  // Generates a salt. Returns |false| on fatal error.
  bool GenerateSalt();

  // Makes a challenge request against the salt. Returns |false| on fatal error.
  bool StartGeneratingSaltSignature();

  // Creates a TPM-protected signature-sealed secret.
  bool CreateTpmProtectedSecret();

  // Called when signature for the salt is received.
  void OnSaltChallengeResponse(std::unique_ptr<brillo::Blob> salt_signature);

  // Generates the result if all necessary pieces are computed.
  void ProceedIfComputationsDone();

  // Constructs the SignatureChallengeInfo protobuf that will be persisted as
  // part of the vault keyset.
  KeysetSignatureChallengeInfo ConstructKeysetSignatureChallengeInfo() const;

  Tpm* const tpm_;
  const brillo::Blob delegate_blob_;
  const brillo::Blob delegate_secret_;
  const std::string account_id_;
  const KeyData key_data_;
  const std::vector<std::map<uint32_t, brillo::Blob>> pcr_restrictions_;
  CompletionCallback completion_callback_;
  SignatureSealingBackend* const signature_sealing_backend_;
  ChallengePublicKeyInfo public_key_info_;
  brillo::Blob salt_;
  ChallengeSignatureAlgorithm salt_signature_algorithm_ =
      CHALLENGE_RSASSA_PKCS1_V1_5_SHA1;
  std::unique_ptr<brillo::Blob> salt_signature_;
  std::unique_ptr<brillo::SecureBlob> tpm_protected_secret_value_;
  SignatureSealedData tpm_sealed_secret_data_;
  base::WeakPtrFactory<ChallengeCredentialsGenerateNewOperation>
      weak_ptr_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_GENERATE_NEW_OPERATION_H_
