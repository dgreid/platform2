// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/double_wrapped_compat_auth_block.h"

#include <memory>
#include <utility>

#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"

namespace cryptohome {

DoubleWrappedCompatAuthBlock::DoubleWrappedCompatAuthBlock(Tpm* tpm,
                                                           TpmInit* tpm_init)
    : tpm_auth_block_(tpm, tpm_init), lib_scrypt_compat_auth_block_() {}

bool DoubleWrappedCompatAuthBlock::Create(const AuthInput& user_input,
                                          AuthBlockState* state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  LOG(FATAL) << "Cannot create a keyset wrapped with both scrypt and TPM.";
  return false;
}

bool DoubleWrappedCompatAuthBlock::Derive(const AuthInput& auth_input,
                                          const AuthBlockState& state,
                                          KeyBlobs* key_blobs,
                                          CryptoError* error) {
  const SerializedVaultKeyset& serialized = state.vault_keyset.value();

  DCHECK((serialized.flags() & SerializedVaultKeyset::SCRYPT_WRAPPED) &&
         (serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED));

  if (lib_scrypt_compat_auth_block_.Derive(auth_input, state, key_blobs,
                                           error)) {
    return true;
  }

  return tpm_auth_block_.Derive(auth_input, state, key_blobs, error);
}

}  // namespace cryptohome
