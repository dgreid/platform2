// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include <string>
#include <utility>

#include <base/logging.h>
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

// LibScrypt requires a salt to be passed from Create() into the encryption
// phase, so this struct has an optional salt.
struct LibScryptCompatKeyObjects {
  // Constructors to make code readable when this class is created.
  explicit LibScryptCompatKeyObjects(brillo::SecureBlob derived_key)
      : derived_key_(derived_key), salt_(base::nullopt) {}

  LibScryptCompatKeyObjects(brillo::SecureBlob derived_key,
                            brillo::SecureBlob salt)
      : derived_key_(derived_key), salt_(salt) {}

  brillo::SecureBlob derived_key() const { return derived_key_; }

  brillo::SecureBlob salt() const {
    if (salt_ == base::nullopt) {
      LOG(FATAL) << "Salt is undefined. Salt is only exposed in the Create() "
                    "flow of the LibScryptCompatAuthBlock.";
    }
    return salt_.value();
  }

 private:
  // These are non-const so the class is assignable, not because they should be
  // modified after construction.

  // The scrypt derived key which must always be present.
  brillo::SecureBlob derived_key_;
  // The salt which only is passed out in the Create() flow.
  base::Optional<brillo::SecureBlob> salt_;
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

  // The following fields are for libscrypt compatibility:
  base::Optional<LibScryptCompatKeyObjects> scrypt_key;
  // The key for scrypt wrapped chaps key.
  base::Optional<LibScryptCompatKeyObjects> chaps_scrypt_key;
  // The scrypt wrapped reset seed.
  base::Optional<LibScryptCompatKeyObjects> scrypt_wrapped_reset_seed_key;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
