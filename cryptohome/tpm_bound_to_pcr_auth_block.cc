// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_bound_to_pcr_auth_block.h"

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

TpmBoundToPcrAuthBlock::TpmBoundToPcrAuthBlock(Tpm* tpm, TpmInit* tpm_init)
    : tpm_(tpm), tpm_init_(tpm_init), utils_(tpm, tpm_init) {
  CHECK(tpm != nullptr);
  CHECK(tpm_init != nullptr);
}

base::Optional<AuthBlockState> TpmBoundToPcrAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  const brillo::SecureBlob& vault_key = user_input.user_input.value();
  const brillo::SecureBlob& salt = user_input.salt.value();
  const std::string& obfuscated_username =
      user_input.obfuscated_username.value();

  // If the cryptohome key isn't loaded, try to load it.
  if (!tpm_init_->HasCryptohomeKey())
    tpm_init_->SetupTpm(/*load_key=*/true);

  // If the key still isn't loaded, fail the operation.
  if (!tpm_init_->HasCryptohomeKey())
    return base::nullopt;

  const auto vkk_key = CryptoLib::CreateSecureRandomBlob(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!CryptoLib::DeriveSecretsScrypt(vault_key, salt, {&pass_blob, &vkk_iv}))
    return base::nullopt;

  std::map<uint32_t, std::string> default_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, std::string> extended_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

  // Encrypt the VKK using the TPM and the user's passkey. The output is two
  // encrypted blobs, sealed to PCR in |tpm_key| and |extended_tpm_key|,
  // which are stored in the serialized vault keyset.
  brillo::SecureBlob tpm_key;
  if (tpm_->SealToPcrWithAuthorization(tpm_init_->GetCryptohomeKey(), vkk_key,
                                       pass_blob, default_pcr_map,
                                       &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return base::nullopt;
  }

  brillo::SecureBlob extended_tpm_key;
  if (tpm_->SealToPcrWithAuthorization(
          tpm_init_->GetCryptohomeKey(), vkk_key, pass_blob, extended_pcr_map,
          &extended_tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds for extended PCR.";
    return base::nullopt;
  }

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  SerializedVaultKeyset serialized;
  brillo::SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(), &pub_key_hash) ==
      Tpm::kTpmRetryNone) {
    serialized.set_tpm_public_key_hash(pub_key_hash.data(),
                                       pub_key_hash.size());
  }

  serialized.set_flags(SerializedVaultKeyset::TPM_WRAPPED |
                       SerializedVaultKeyset::SCRYPT_DERIVED |
                       SerializedVaultKeyset::PCR_BOUND);
  serialized.set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized.set_extended_tpm_key(extended_tpm_key.data(),
                                  extended_tpm_key.size());

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = vkk_key;
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;
  key_blobs->auth_iv = vkk_iv;

  return {{serialized}};
}

bool TpmBoundToPcrAuthBlock::Derive(const AuthInput& auth_input,
                                    const AuthBlockState& state,
                                    KeyBlobs* key_out_data,
                                    CryptoError* error) {
  const SerializedVaultKeyset& serialized = state.vault_keyset.value();
  if (!utils_.CheckTPMReadiness(serialized, error))
    return false;

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  bool locked_to_single_user = auth_input.locked_to_single_user.value_or(false);
  brillo::SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  std::string tpm_key_str = locked_to_single_user
                                ? serialized.extended_tpm_key()
                                : serialized.tpm_key();
  brillo::SecureBlob tpm_key(tpm_key_str.begin(), tpm_key_str.end());

  if (!DecryptTpmBoundToPcr(auth_input.user_input.value(), tpm_key, salt, error,
                            &key_out_data->vkk_iv.value(),
                            &key_out_data->vkk_key.value())) {
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

bool TpmBoundToPcrAuthBlock::DecryptTpmBoundToPcr(
    const brillo::SecureBlob& vault_key,
    const brillo::SecureBlob& tpm_key,
    const brillo::SecureBlob& salt,
    CryptoError* error,
    brillo::SecureBlob* vkk_iv,
    brillo::SecureBlob* vkk_key) const {
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  if (!CryptoLib::DeriveSecretsScrypt(vault_key, salt, {&pass_blob, vkk_iv})) {
    return false;
  }

  Tpm::TpmRetryAction retry_action;
  for (int i = 0; i < kTpmDecryptMaxRetries; ++i) {
    std::map<uint32_t, std::string> pcr_map({{kTpmSingleUserPCR, ""}});
    retry_action = tpm_->UnsealWithAuthorization(
        tpm_init_->GetCryptohomeKey(), tpm_key, pass_blob, pcr_map, vkk_key);

    if (retry_action == Tpm::kTpmRetryNone)
      return true;

    if (!TpmAuthBlockUtils::TpmErrorIsRetriable(retry_action))
      break;

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      break;
    }
  }

  LOG(ERROR) << "Failed to unwrap VKK with creds.";
  *error = TpmAuthBlockUtils::TpmErrorToCrypto(retry_action);
  return false;
}

}  // namespace cryptohome
