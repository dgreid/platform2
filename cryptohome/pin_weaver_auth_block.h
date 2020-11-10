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
  explicit PinWeaverAuthBlock(LECredentialManager* le_manager,
                              TpmInit* tpm_init);
  PinWeaverAuthBlock(const PinWeaverAuthBlock&) = delete;
  PinWeaverAuthBlock& operator=(const PinWeaverAuthBlock&) = delete;

  base::Optional<AuthBlockState> Create(const AuthInput& user_input,
                                        KeyBlobs* key_blobs,
                                        CryptoError* error) override;

  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;

 private:
  // Handler for Low Entropy credentials.
  LECredentialManager* le_manager_;

  TpmInit* tpm_init_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PIN_WEAVER_AUTH_BLOCK_H_
