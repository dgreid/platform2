// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_NOT_BOUND_TO_PCR_AUTH_BLOCK_H_
#define CRYPTOHOME_TPM_NOT_BOUND_TO_PCR_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>

#include "cryptohome/crypto.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmNotBoundToPcrAuthBlock : public AuthBlock {
 public:
  TpmNotBoundToPcrAuthBlock(Tpm* tpm, TpmInit* tpm_init);

  base::Optional<AuthBlockState> Create(const AuthInput& user_input,
                                        KeyBlobs* key_blobs,
                                        CryptoError* error) override;

  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;

 private:
  // Decrypt the |vault_key| that is not bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmNotBoundToPcr(const SerializedVaultKeyset& serialized,
                               const brillo::SecureBlob& vault_key,
                               const brillo::SecureBlob& tpm_key,
                               const brillo::SecureBlob& salt,
                               CryptoError* error,
                               brillo::SecureBlob* vkk_iv,
                               brillo::SecureBlob* vkk_key) const;

  Tpm* tpm_;
  TpmInit* tpm_init_;
  TpmAuthBlockUtils utils_;

  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptNotBoundToPcrTest);

  DISALLOW_COPY_AND_ASSIGN(TpmNotBoundToPcrAuthBlock);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_NOT_BOUND_TO_PCR_AUTH_BLOCK_H_
