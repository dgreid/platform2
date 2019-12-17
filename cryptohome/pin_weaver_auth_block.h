// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PIN_WEAVER_AUTH_BLOCK_H_
#define CRYPTOHOME_PIN_WEAVER_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include "cryptohome/le_credential_manager.h"

#include <base/macros.h>

namespace cryptohome {

class PinWeaverAuthBlock : public AuthBlock {
 public:
  explicit PinWeaverAuthBlock(LECredentialManager* le_manager);

  bool Derive(const AuthInput& user_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;
 private:
  // Handler for Low Entropy credentials.
  // TODO(kerrnel): This is a pointer for now because the object is owned by
  // Crypto class, but later this should be a singleton that's owned by the
  // AuthBlock.
  LECredentialManager* le_manager_;

  DISALLOW_COPY_AND_ASSIGN(PinWeaverAuthBlock);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PIN_WEAVER_AUTH_BLOCK_H_
