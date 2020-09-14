// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_AUTH_BLOCK_UTILS_H_
#define CRYPTOHOME_TPM_AUTH_BLOCK_UTILS_H_

#include <base/macros.h>

#include <string>

#include "cryptohome/crypto_error.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

class TpmAuthBlockUtils {
 public:
  TpmAuthBlockUtils(Tpm* tpm, TpmInit* tpm_init);

  // A static method which converts an error object.
  static CryptoError TpmErrorToCrypto(Tpm::TpmRetryAction retry_action);

  // A static method to report which errors can be recovered from with a retry.
  static bool TpmErrorIsRetriable(Tpm::TpmRetryAction retry_action);

  // Checks if the specified |hash| is the same as the hash for the |tpm_| used
  // by the class.
  bool IsTPMPubkeyHash(const std::string& hash, CryptoError* error) const;

  // This checks that the TPM is ready and that the vault keyset was encrypted
  // with this machine's TPM.
  bool CheckTPMReadiness(const SerializedVaultKeyset& serialized,
                         CryptoError* error);

 private:
  Tpm* tpm_;
  TpmInit* tpm_init_;

  DISALLOW_COPY_AND_ASSIGN(TpmAuthBlockUtils);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_AUTH_BLOCK_UTILS_H_
