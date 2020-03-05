// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_OBJECTS_H_
#define CRYPTOHOME_KEY_OBJECTS_H_

#include "cryptohome/cryptolib.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

struct AuthInput {
  // The user input, such as password.
  base::Optional<brillo::SecureBlob> user_input;
  // Whether or not the PCR is extended, this is usually false.
  base::Optional<bool> is_pcr_extended;
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
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_OBJECTS_H_
