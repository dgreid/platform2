// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"

#include <map>
#include <string>

#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

TpmNotBoundToPcrAuthBlock::TpmNotBoundToPcrAuthBlock(Tpm* tpm,
                                                     TpmInit* tpm_init)
    : tpm_(tpm), tpm_init_(tpm_init), utils_(tpm, tpm_init) {
  CHECK(tpm != nullptr);
  CHECK(tpm_init != nullptr);
}

bool TpmNotBoundToPcrAuthBlock::Derive(const AuthInput& auth_input,
                                       const AuthBlockState& state,
                                       KeyBlobs* key_out_data,
                                       CryptoError* error) {
  const SerializedVaultKeyset& serialized = state.vault_keyset.value();
  if (!utils_.CheckTPMReadiness(serialized, error))
    return false;

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  brillo::SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  brillo::SecureBlob tpm_key(serialized.tpm_key().begin(),
                             serialized.tpm_key().end());

  if (!DecryptTpmNotBoundToPcr(
          serialized, auth_input.user_input.value(), tpm_key, salt, error,
          &key_out_data->vkk_iv.value(), &key_out_data->vkk_key.value())) {
    return false;
  }

  key_out_data->chaps_iv = key_out_data->vkk_iv;
  key_out_data->authorization_data_iv = key_out_data->vkk_iv;
  key_out_data->wrapped_reset_seed = brillo::SecureBlob();
  key_out_data->wrapped_reset_seed.value().assign(
      serialized.wrapped_reset_seed().begin(),
      serialized.wrapped_reset_seed().end());

  if (!serialized.has_tpm_public_key_hash() && error) {
    *error = CryptoError::CE_NO_PUBLIC_KEY_HASH;
  }

  return true;
}

bool TpmNotBoundToPcrAuthBlock::DecryptTpmNotBoundToPcr(
    const SerializedVaultKeyset& serialized,
    const brillo::SecureBlob& vault_key,
    const brillo::SecureBlob& tpm_key,
    const brillo::SecureBlob& salt,
    CryptoError* error,
    brillo::SecureBlob* vkk_iv,
    brillo::SecureBlob* vkk_key) const {
  brillo::SecureBlob aes_skey(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  brillo::SecureBlob local_vault_key(vault_key.begin(), vault_key.end());
  unsigned int rounds;
  if (serialized.has_password_rounds()) {
    rounds = serialized.password_rounds();
  } else {
    rounds = kDefaultLegacyPasswordRounds;
  }

  bool scrypt_derived =
      serialized.flags() & SerializedVaultKeyset::SCRYPT_DERIVED;
  if (scrypt_derived) {
    if (!CryptoLib::DeriveSecretsScrypt(vault_key, salt,
                                        {&aes_skey, &kdf_skey, vkk_iv})) {
      PopulateError(error, CryptoError::CE_OTHER_FATAL);
      return false;
    }
  } else {
    CryptoLib::PasskeyToAesKey(vault_key, salt, rounds, &aes_skey, NULL);
  }

  for (int i = 0; i < kTpmDecryptMaxRetries; i++) {
    Tpm::TpmRetryAction retry_action =
        tpm_->DecryptBlob(tpm_init_->GetCryptohomeKey(), tpm_key, aes_skey,
                          std::map<uint32_t, std::string>(), &local_vault_key);

    if (retry_action == Tpm::kTpmRetryNone)
      break;

    if (!TpmAuthBlockUtils::TpmErrorIsRetriable(retry_action)) {
      LOG(ERROR) << "Failed to unwrap VKK with creds.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmAuthBlockUtils::TpmErrorToCrypto(retry_action);
      return false;
    }

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmAuthBlockUtils::TpmErrorToCrypto(Tpm::kTpmRetryFailNoRetry);
      return false;
    }
  }

  if (scrypt_derived) {
    *vkk_key = CryptoLib::HmacSha256(kdf_skey, local_vault_key);
  } else {
    if (!CryptoLib::PasskeyToAesKey(local_vault_key, salt, rounds, vkk_key,
                                    vkk_iv)) {
      LOG(ERROR) << "Failure converting IVKK to VKK.";
      PopulateError(error, CryptoError::CE_OTHER_FATAL);
      return false;
    }
  }
  return true;
}

}  // namespace cryptohome
