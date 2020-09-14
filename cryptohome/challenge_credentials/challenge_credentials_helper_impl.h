// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <base/threading/thread_checker.h>
#include <brillo/secure_blob.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/challenge_credentials/challenge_credentials_operation.h"
#include "cryptohome/credentials.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

// Real implementation of ChallengeCredentialsHelper that is based on TPM and
// other cryptographic operations.
class ChallengeCredentialsHelperImpl final : public ChallengeCredentialsHelper {
 public:
  // The maximum number of attempts that will be made for a single operation
  // when it fails with a transient error.
  static constexpr int kRetryAttemptCount = 3;

  // |tpm| is a non-owned pointer that must stay valid for the whole lifetime of
  // the created object.
  // |delegate_blob| and |delegate_secret| should correspond to a TPM delegate
  // that allows doing signature-sealing operations (currently used only on TPM
  // 1.2).
  ChallengeCredentialsHelperImpl(Tpm* tpm,
                                 const brillo::Blob& delegate_blob,
                                 const brillo::Blob& delegate_secret);
  ChallengeCredentialsHelperImpl(const ChallengeCredentialsHelperImpl&) =
      delete;
  ChallengeCredentialsHelperImpl& operator=(
      const ChallengeCredentialsHelperImpl&) = delete;
  ~ChallengeCredentialsHelperImpl() override;

  // ChallengeCredentialsHelper:
  void GenerateNew(
      const std::string& account_id,
      const KeyData& key_data,
      const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions,
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      GenerateNewCallback callback) override;
  void Decrypt(const std::string& account_id,
               const KeyData& key_data,
               const KeysetSignatureChallengeInfo& keyset_challenge_info,
               std::unique_ptr<KeyChallengeService> key_challenge_service,
               DecryptCallback callback) override;
  void VerifyKey(const std::string& account_id,
                 const KeyData& key_data,
                 std::unique_ptr<KeyChallengeService> key_challenge_service,
                 VerifyKeyCallback callback) override;

 private:
  void StartDecryptOperation(
      const std::string& account_id,
      const KeyData& key_data,
      const KeysetSignatureChallengeInfo& keyset_challenge_info,
      int attempt_number,
      DecryptCallback callback);

  // Aborts the currently running operation, if any, and destroys all resources
  // associated with it.
  void CancelRunningOperation();

  // Wrapper for the completion callback of GenerateNew(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnGenerateNewCompleted(GenerateNewCallback original_callback,
                              std::unique_ptr<Credentials> credentials);

  // Wrapper for the completion callback of Decrypt(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnDecryptCompleted(
      const std::string& account_id,
      const KeyData& key_data,
      const KeysetSignatureChallengeInfo& keyset_challenge_info,
      int attempt_number,
      DecryptCallback original_callback,
      Tpm::TpmRetryAction retry_action,
      std::unique_ptr<Credentials> credentials);

  // Wrapper for the completion callback of VerifyKey(). Cleans up resources
  // associated with the operation and forwards results to the original
  // callback.
  void OnVerifyKeyCompleted(VerifyKeyCallback original_callback,
                            bool is_key_valid);

  // Non-owned.
  Tpm* const tpm_;
  // TPM delegate that was passed to the constructor.
  const brillo::Blob delegate_blob_;
  const brillo::Blob delegate_secret_;
  // The key challenge service used for the currently running operation, if any.
  std::unique_ptr<KeyChallengeService> key_challenge_service_;
  // The state of the currently running operation, if any.
  std::unique_ptr<ChallengeCredentialsOperation> operation_;

  base::ThreadChecker thread_checker_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_HELPER_IMPL_H_
