// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_AUTH_BLOCK_H_
#define CRYPTOHOME_TPM_AUTH_BLOCK_H_

#include "cryptohome/auth_block.h"

#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>

#include "cryptohome/crypto.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmAuthBlock : public AuthBlock {
 public:
  TpmAuthBlock(Tpm* tpm, TpmInit* tpm_init);

  bool Derive(const AuthInput& auth_input,
              const AuthBlockState& state,
              KeyBlobs* key_blobs,
              CryptoError* error) override;

 private:
  // Checks if the specified |hash| is the same as the hash for the |tpm_| used
  // by the class.
  bool IsTPMPubkeyHash(const std::string& hash,
                       CryptoError* error) const;

  // Returns the tpm_key data taken from |serialized|, specifically if the
  // keyset is PCR_BOUND and |locked_to_single_user| the data is taken from
  // extended_tpm_key. Otherwise the data from tpm_key is used.
  brillo::SecureBlob GetTpmKeyFromSerialized(
      const SerializedVaultKeyset& serialized,
      bool locked_to_single_user) const;

  // Decrypt the |vault_key| that is not bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmNotBoundToPcr(const SerializedVaultKeyset& serialized,
                               const brillo::SecureBlob& vault_key,
                               const brillo::SecureBlob& tpm_key,
                               const brillo::SecureBlob& salt,
                               CryptoError* error,
                               brillo::SecureBlob* vkk_iv,
                               brillo::SecureBlob* vkk_key) const;

  // Decrypt the |vault_key| that is bound to PCR, returning the |vkk_iv|
  // and |vkk_key|.
  bool DecryptTpmBoundToPcr(const brillo::SecureBlob& vault_key,
                            const brillo::SecureBlob& tpm_key,
                            const brillo::SecureBlob& salt,
                            CryptoError* error,
                            brillo::SecureBlob* vkk_iv,
                            brillo::SecureBlob* vkk_key) const;

  Tpm* tpm_;
  TpmInit* tpm_init_;

  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptBoundToPcrTest);
  FRIEND_TEST_ALL_PREFIXES(TPMAuthBlockTest, DecryptNotBoundToPcrTest);

  DISALLOW_COPY_AND_ASSIGN(TpmAuthBlock);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_AUTH_BLOCK_H_
