// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCK_H_
#define CRYPTOHOME_AUTH_BLOCK_H_

#include "cryptohome/crypto_error.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/vault_keyset.h"

#include <base/optional.h>

namespace cryptohome {

struct AuthBlockState {
  base::Optional<SerializedVaultKeyset> vault_keyset;
};

// This is a pure virtual interface designed to be implemented by the different
// authentication methods - U2F, PinWeaver, TPM backed passwords, etc. - so that
// they take some arbitrary user input and give out a key.
class AuthBlock {
 public:
  virtual ~AuthBlock() = default;

  // This is implemented by concrete auth methods to create a fresh key from
  // user input. The key will then be used to wrap the keyset.
  virtual bool Create(const AuthInput& user_input,
                      AuthBlockState* state,
                      KeyBlobs* key_blobs,
                      CryptoError* error) {
    // TODO(kerrnel): Once all AuthBlocks have a Create() method, make this pure
    // virtual.
    return false;
  }

  // This is implemented by concrete auth methods to map the user secret
  // input into a key. This method should successfully authenticate the user.
  virtual bool Derive(const AuthInput& auth_input,
                      const AuthBlockState& state,
                      KeyBlobs* key_blobs,
                      CryptoError* error) = 0;

 protected:
  // This is a virtual interface that should not be directly constructed.
  AuthBlock() = default;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCK_H_
