// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include <string>

#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptolib.h"
#include "cryptohome/vault_keyset.h"

namespace cryptohome {

struct AuthInput {
  // The user input, such as password.
  base::Optional<brillo::SecureBlob> user_input;
  // Whether or not the PCR is extended, this is usually false.
  base::Optional<bool> locked_to_single_user;
  // If a salt is to be used during credential generation.
  base::Optional<brillo::SecureBlob> salt;
  // The obfuscated username.
  base::Optional<std::string> obfuscated_username;
  // A generated reset secret to unlock a rate limited credential.
  base::Optional<brillo::SecureBlob> reset_secret;
};

// This struct is populated by the various authentication methods, with the
// secrets derived from the user input.
struct KeyBlobs {
  // The file encryption key.
  base::Optional<brillo::SecureBlob> vkk_key;
  // The file encryption IV.
  base::Optional<brillo::SecureBlob> vkk_iv;
  // The IV to use with the chaps key.
  base::Optional<brillo::SecureBlob> chaps_iv;
  // The IV to use with the authorization data.
  base::Optional<brillo::SecureBlob> auth_iv;
  // The wrapped reset seet, if it should be unwrapped.
  base::Optional<brillo::SecureBlob> wrapped_reset_seed;
  // The IV used to decrypt the authorization data.
  base::Optional<brillo::SecureBlob> authorization_data_iv;
  // The reset secret used for LE credentials.
  base::Optional<brillo::SecureBlob> reset_secret;
  // The key used for existing data encrypted with libscrypt.
  base::Optional<brillo::SecureBlob> scrypt_key;
  // The key for scrypt wrapped chaps key.
  base::Optional<brillo::SecureBlob> chaps_scrypt_key;
  // The scrypt wrapped reset seed.
  base::Optional<brillo::SecureBlob> scrypt_wrapped_reset_seed_key;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
