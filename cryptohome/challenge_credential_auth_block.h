// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
#define CRYPTOHOME_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_

#include "cryptohome/libscrypt_compat_auth_block.h"

namespace cryptohome {

class ChallengeCredentialAuthBlock : public LibScryptCompatAuthBlock {
 public:
  ChallengeCredentialAuthBlock() = default;
  ~ChallengeCredentialAuthBlock() = default;

  base::Optional<AuthBlockState> Create(const AuthInput& user_input,
                                        KeyBlobs* key_blobs,
                                        CryptoError* error) override;

  // This derives a high entropy secret from the input secret provided by the
  // challenge credential.
  bool Derive(const AuthInput& user_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CHALLENGE_CREDENTIAL_AUTH_BLOCK_H_
