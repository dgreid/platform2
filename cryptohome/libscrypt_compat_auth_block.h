// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_
#define CRYPTOHOME_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

namespace cryptohome {

class LibScryptCompatAuthBlock : public AuthBlock {
 public:
  LibScryptCompatAuthBlock() = default;
  ~LibScryptCompatAuthBlock() = default;

  // This uses Scrypt to derive high entropy keys from the user's password.
  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_LIBSCRYPT_COMPAT_AUTH_BLOCK_H_
