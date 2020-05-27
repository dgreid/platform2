// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credential_auth_block.h"

#include "cryptohome/key_objects.h"
#include "cryptohome/libscrypt_compat_auth_block.h"

namespace cryptohome {

bool ChallengeCredentialAuthBlock::Derive(const AuthInput& user_input,
                                          const AuthBlockState& state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  const SerializedVaultKeyset& serialized = state.vault_keyset.value();
  if (!(serialized.flags() & SerializedVaultKeyset::SCRYPT_WRAPPED)) {
    LOG(ERROR) << "Invalid flags for challenge-protected keyset";
    *error = CryptoError::CE_OTHER_FATAL;
    return false;
  }

  return LibScryptCompatAuthBlock::Derive(user_input, state, key_blobs, error);
}

}  // namespace cryptohome
