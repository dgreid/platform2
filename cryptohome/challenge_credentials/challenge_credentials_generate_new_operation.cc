// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credentials/challenge_credentials_generate_new_operation.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/optional.h>

#include "cryptohome/challenge_credentials/challenge_credentials_constants.h"
#include "cryptohome/credentials.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/signature_sealed_data.pb.h"
#include "cryptohome/tpm.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::BlobToString;
using brillo::CombineBlobs;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

std::vector<ChallengeSignatureAlgorithm> GetSealingAlgorithms(
    const ChallengePublicKeyInfo& public_key_info) {
  std::vector<ChallengeSignatureAlgorithm> sealing_algorithms;
  for (int index = 0; index < public_key_info.signature_algorithm_size();
       ++index) {
    sealing_algorithms.push_back(public_key_info.signature_algorithm(index));
  }
  return sealing_algorithms;
}

// Returns the signature algorithm that should be used for signing salt from the
// set of algorithms supported by the given key. Returns nullopt when no
// suitable algorithm was found.
base::Optional<ChallengeSignatureAlgorithm> ChooseSaltSignatureAlgorithm(
    const ChallengePublicKeyInfo& public_key_info) {
  DCHECK(public_key_info.signature_algorithm_size());
  base::Optional<ChallengeSignatureAlgorithm> currently_chosen_algorithm;
  // Respect the input's algorithm prioritization, with the exception of
  // considering SHA-1 as the least preferred option.
  for (int index = 0; index < public_key_info.signature_algorithm_size();
       ++index) {
    currently_chosen_algorithm = public_key_info.signature_algorithm(index);
    if (*currently_chosen_algorithm != CHALLENGE_RSASSA_PKCS1_V1_5_SHA1)
      break;
  }
  return currently_chosen_algorithm;
}

}  // namespace

ChallengeCredentialsGenerateNewOperation::
    ChallengeCredentialsGenerateNewOperation(
        KeyChallengeService* key_challenge_service,
        Tpm* tpm,
        const brillo::Blob& delegate_blob,
        const brillo::Blob& delegate_secret,
        const std::string& account_id,
        const KeyData& key_data,
        const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
        CompletionCallback completion_callback)
    : ChallengeCredentialsOperation(key_challenge_service),
      tpm_(tpm),
      delegate_blob_(delegate_blob),
      delegate_secret_(delegate_secret),
      account_id_(account_id),
      key_data_(key_data),
      pcr_restrictions_(pcr_restrictions),
      completion_callback_(std::move(completion_callback)),
      signature_sealing_backend_(tpm_->GetSignatureSealingBackend()) {
  DCHECK_EQ(key_data.type(), KeyData::KEY_TYPE_CHALLENGE_RESPONSE);
}

ChallengeCredentialsGenerateNewOperation::
    ~ChallengeCredentialsGenerateNewOperation() = default;

void ChallengeCredentialsGenerateNewOperation::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!StartProcessing()) {
    LOG(ERROR) << "Failed to start the generation operation";
    Abort();
    // |this| can be already destroyed at this point.
  }
}

void ChallengeCredentialsGenerateNewOperation::Abort() {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Invalidate weak pointers in order to cancel all jobs that are currently
  // waiting, to prevent them from running and consuming resources after our
  // abortion (in case |this| doesn't get destroyed immediately).
  //
  // Note that the already issued challenge requests don't get cancelled, so
  // their responses will be just ignored should they arrive later. The request
  // cancellation is not supported by the challenges IPC API currently, neither
  // it is supported by the API for smart card drivers in Chrome OS.
  weak_ptr_factory_.InvalidateWeakPtrs();
  Complete(&completion_callback_, nullptr /* credentials */);
  // |this| can be already destroyed at this point.
}

bool ChallengeCredentialsGenerateNewOperation::StartProcessing() {
  if (!signature_sealing_backend_) {
    LOG(ERROR) << "Signature sealing is disabled";
    return false;
  }
  if (!key_data_.challenge_response_key_size()) {
    LOG(ERROR) << "Missing challenge-response key information";
    return false;
  }
  if (key_data_.challenge_response_key_size() > 1) {
    LOG(ERROR)
        << "Using multiple challenge-response keys at once is unsupported";
    return false;
  }
  public_key_info_ = key_data_.challenge_response_key(0);
  if (!public_key_info_.signature_algorithm_size()) {
    LOG(ERROR) << "The key does not support any signature algorithm";
    return false;
  }
  if (!GenerateSalt() || !StartGeneratingSaltSignature())
    return false;
  // TODO(crbug.com/842791): This is buggy: |this| may be already deleted by
  // that point, in case when the salt's challenge request failed synchronously.
  if (!CreateTpmProtectedSecret())
    return false;
  ProceedIfComputationsDone();
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::GenerateSalt() {
  Blob salt_random_bytes;
  if (!tpm_->GetRandomDataBlob(kChallengeCredentialsSaltRandomByteCount,
                               &salt_random_bytes)) {
    LOG(ERROR) << "Failed to generate random bytes for the salt";
    return false;
  }
  DCHECK_EQ(kChallengeCredentialsSaltRandomByteCount, salt_random_bytes.size());
  // IMPORTANT: Make sure the salt is prefixed with a constant. See the comment
  // on GetChallengeCredentialsSaltConstantPrefix() for details.
  salt_ = CombineBlobs(
      {GetChallengeCredentialsSaltConstantPrefix(), salt_random_bytes});
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::StartGeneratingSaltSignature() {
  DCHECK(!salt_.empty());
  base::Optional<ChallengeSignatureAlgorithm> chosen_salt_signature_algorithm =
      ChooseSaltSignatureAlgorithm(public_key_info_);
  if (!chosen_salt_signature_algorithm) {
    LOG(ERROR) << "Failed to choose salt signature algorithm";
    return false;
  }
  salt_signature_algorithm_ = *chosen_salt_signature_algorithm;
  MakeKeySignatureChallenge(
      account_id_, BlobFromString(public_key_info_.public_key_spki_der()),
      salt_, salt_signature_algorithm_,
      base::Bind(
          &ChallengeCredentialsGenerateNewOperation::OnSaltChallengeResponse,
          weak_ptr_factory_.GetWeakPtr()));
  return true;
}

bool ChallengeCredentialsGenerateNewOperation::CreateTpmProtectedSecret() {
  SecureBlob local_tpm_protected_secret_value;
  if (!signature_sealing_backend_->CreateSealedSecret(
          BlobFromString(public_key_info_.public_key_spki_der()),
          GetSealingAlgorithms(public_key_info_), pcr_restrictions_,
          delegate_blob_, delegate_secret_, &local_tpm_protected_secret_value,
          &tpm_sealed_secret_data_)) {
    LOG(ERROR) << "Failed to create TPM-protected secret";
    return false;
  }
  DCHECK(local_tpm_protected_secret_value.size());
  tpm_protected_secret_value_ =
      std::make_unique<SecureBlob>(std::move(local_tpm_protected_secret_value));
  return true;
}

void ChallengeCredentialsGenerateNewOperation::OnSaltChallengeResponse(
    std::unique_ptr<Blob> salt_signature) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!salt_signature) {
    LOG(ERROR) << "Salt signature challenge failed";
    Abort();
    // |this| can be already destroyed at this point.
    return;
  }
  salt_signature_ = std::move(salt_signature);
  ProceedIfComputationsDone();
}

void ChallengeCredentialsGenerateNewOperation::ProceedIfComputationsDone() {
  if (!salt_signature_ || !tpm_protected_secret_value_)
    return;
  auto credentials = std::make_unique<Credentials>(
      account_id_,
      ConstructPasskey(*tpm_protected_secret_value_, *salt_signature_));
  credentials->set_key_data(key_data_);
  credentials->set_challenge_credentials_keyset_info(
      ConstructKeysetSignatureChallengeInfo());
  Complete(&completion_callback_, std::move(credentials));
  // |this| can be already destroyed at this point.
}

ChallengeCredentialsGenerateNewOperation::KeysetSignatureChallengeInfo
ChallengeCredentialsGenerateNewOperation::
    ConstructKeysetSignatureChallengeInfo() const {
  KeysetSignatureChallengeInfo keyset_signature_challenge_info;
  keyset_signature_challenge_info.set_public_key_spki_der(
      public_key_info_.public_key_spki_der());
  *keyset_signature_challenge_info.mutable_sealed_secret() =
      tpm_sealed_secret_data_;
  keyset_signature_challenge_info.set_salt(BlobToString(salt_));
  keyset_signature_challenge_info.set_salt_signature_algorithm(
      salt_signature_algorithm_);
  return keyset_signature_challenge_info;
}

}  // namespace cryptohome
