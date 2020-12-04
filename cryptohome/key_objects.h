// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include <memory>
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
class LibScryptCompatKeyObjects {
 public:
  // This class is never usable for encryption without a salt.
  explicit LibScryptCompatKeyObjects(brillo::SecureBlob derived_key)
      : derived_key_(derived_key), salt_(base::nullopt) {}

  LibScryptCompatKeyObjects(brillo::SecureBlob derived_key,
                            brillo::SecureBlob salt)
      : derived_key_(derived_key), salt_(salt) {}

  // Prohibit copy/move/assignment.
  LibScryptCompatKeyObjects(const LibScryptCompatKeyObjects&) = delete;
  LibScryptCompatKeyObjects(const LibScryptCompatKeyObjects&&) = delete;
  LibScryptCompatKeyObjects& operator=(const LibScryptCompatKeyObjects&) =
      delete;
  LibScryptCompatKeyObjects& operator=(const LibScryptCompatKeyObjects&&) =
      delete;

  // Access the derived key.
  brillo::SecureBlob derived_key();

  // Access the salt. The salt isn't used for decryption, so this only returns
  // the salt if the object is safe to used for encryption. Once accessed, the
  // salt is cleared and the class is no longer usable for encryption.
  brillo::SecureBlob ConsumeSalt();

 private:
  // The scrypt derived key which must always be present.
  const brillo::SecureBlob derived_key_;
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
  // The reset secret used for LE credentials.
  base::Optional<brillo::SecureBlob> reset_secret;

  // The following fields are for libscrypt compatibility. They must be
  // unique_ptr's as the libscrypt keys cannot safely be re-used for multiple
  // encryption operations, so these are destroyed upon use.
  std::unique_ptr<LibScryptCompatKeyObjects> scrypt_key;
  // The key for scrypt wrapped chaps key.
  std::unique_ptr<LibScryptCompatKeyObjects> chaps_scrypt_key;
  // The scrypt wrapped reset seed.
  std::unique_ptr<LibScryptCompatKeyObjects> scrypt_wrapped_reset_seed_key;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
