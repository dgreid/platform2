// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIALS_MOCK_CHALLENGE_CREDENTIALS_HELPER_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIALS_MOCK_CHALLENGE_CREDENTIALS_HELPER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "cryptohome/challenge_credentials/challenge_credentials_helper.h"
#include "cryptohome/credentials.h"
#include "cryptohome/key_challenge_service.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class MockChallengeCredentialsHelper : public ChallengeCredentialsHelper {
 public:
  MockChallengeCredentialsHelper() = default;
  ~MockChallengeCredentialsHelper() = default;

  MOCK_METHOD(void, GenerateNew, (
      const std::string& account_id,
      const KeyData& key_data,
      (const std::vector<std::map<uint32_t, brillo::Blob>>& pcr_restrictions),
      std::unique_ptr<KeyChallengeService> key_challenge_service,
      GenerateNewCallback callback), (override));
  MOCK_METHOD(void, Decrypt, (const std::string& account_id,
               const KeyData& key_data,
               const KeysetSignatureChallengeInfo& keyset_challenge_info,
               std::unique_ptr<KeyChallengeService> key_challenge_service,
               DecryptCallback callback), (override));
  MOCK_METHOD(void, VerifyKey, (const std::string& account_id,
                 const KeyData& key_data,
                 std::unique_ptr<KeyChallengeService> key_challenge_service,
                 VerifyKeyCallback callback), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIALS_MOCK_CHALLENGE_CREDENTIALS_HELPER_H_
