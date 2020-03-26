// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LIBSCRYPT_COMPAT_H_
#define CRYPTOHOME_LIBSCRYPT_COMPAT_H_

#include <brillo/secure_blob.h>

#include <stdint.h>

#include "cryptohome/cryptolib.h"

namespace cryptohome {

extern const size_t kLibScryptSaltSize;
extern const size_t kLibScryptDerivedKeySize;

// This class maintains compatibility with buffers that were previously
// encrypted with the libscrypt specific header. It allows the actual scrypt key
// derivation to be split from the header, encryption, and HMAC.
class LibScryptCompat {
 public:
  // This encrypts the |data_to_encrypt| with the |derived_key|. Although the
  // core of this is standard AES-256-CTR, this is libscrypt specific in that it
  // puts the header in the blob, and then HMACs the encrypted data. This
  // specific format must be preserved for backwards compatibility. USS code
  // will generate an AES-256 key, and the rest of the key hierarchy is
  // universal.
  static bool Encrypt(const brillo::SecureBlob& derived_key,
                      const brillo::SecureBlob& salt,
                      const brillo::SecureBlob& data_to_encrypt,
                      const ScryptParameters& params,
                      brillo::SecureBlob* encrypted_data);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LIBSCRYPT_COMPAT_H_
