// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/challenge_credential_auth_block.h"

#include "cryptohome/key_objects.h"
#include "cryptohome/libscrypt_compat_auth_block.h"

namespace cryptohome {

base::Optional<AuthBlockState> ChallengeCredentialAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  auto auth_state =
      LibScryptCompatAuthBlock::Create(user_input, key_blobs, error);
  if (auth_state == base::nullopt) {
    LOG(ERROR) << "scrypt derivation failed for challenge credential";
    return base::nullopt;
  }

  auth_state->vault_keyset->set_flags(
      auth_state->vault_keyset->flags() |
      SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);

  return auth_state;
}

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
