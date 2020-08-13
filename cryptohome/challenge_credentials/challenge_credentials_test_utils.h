// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_TEST_UTILS_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_TEST_UTILS_H_

#include <memory>
#include <string>

#include <brillo/secure_blob.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/rpc.pb.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class Credentials;

// Structures that hold results returned from ChallengeCredentialsHelper
// operations:

// for ChallengeCredentialsHelper::GenerateNew():
struct ChallengeCredentialsGenerateNewResult {
  std::unique_ptr<Credentials> credentials;
};

// for ChallengeCredentialsHelper::Decrypt():
struct ChallengeCredentialsDecryptResult {
  std::unique_ptr<Credentials> credentials;
};

// Functions that make callbacks for ChallengeCredentialsHelper that store the
// result into the given smart pointer (this smart pointer will become non-null
// after the callback gets executed):

// for ChallengeCredentialsHelper::GenerateNew():
ChallengeCredentialsHelper::GenerateNewCallback
MakeChallengeCredentialsGenerateNewResultWriter(
    std::unique_ptr<ChallengeCredentialsGenerateNewResult>* result);

// for ChallengeCredentialsHelper::Decrypt():
ChallengeCredentialsHelper::DecryptCallback
MakeChallengeCredentialsDecryptResultWriter(
    std::unique_ptr<ChallengeCredentialsDecryptResult>* result);

// Functions that verify that the result returned from the
// ChallengeCredentialsHelper operation is valid:

// for ChallengeCredentialsHelper::GenerateNew():
void VerifySuccessfulChallengeCredentialsGenerateNewResult(
    const ChallengeCredentialsGenerateNewResult& result,
    const std::string& expected_username,
    const brillo::SecureBlob& expected_passkey);

// for ChallengeCredentialsHelper::Decrypt():
void VerifySuccessfulChallengeCredentialsDecryptResult(
    const ChallengeCredentialsDecryptResult& result,
    const std::string& expected_username,
    const brillo::SecureBlob& expected_passkey);

// Functions that verify that the result returned from the
// ChallengeCredentialsHelper operation is a failure result:

// for ChallengeCredentialsHelper::GenerateNew():
void VerifyFailedChallengeCredentialsGenerateNewResult(
    const ChallengeCredentialsGenerateNewResult& result);

// for ChallengeCredentialsHelper::Decrypt():
void VerifyFailedChallengeCredentialsDecryptResult(
    const ChallengeCredentialsDecryptResult& result);

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_CHALLENGE_CREDENTIALS_TEST_UTILS_H_
