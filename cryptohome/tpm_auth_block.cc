// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_auth_block.h"

#include <map>
#include <string>

#include <base/optional.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

namespace {

CryptoError TpmErrorToCrypto(Tpm::TpmRetryAction retry_action) {
  switch (retry_action) {
    case Tpm::kTpmRetryFatal:
      // All errors mapped here will cause re-creating the cryptohome if
      // they occur when decrypting the keyset.
      return CryptoError::CE_TPM_FATAL;
    case Tpm::kTpmRetryCommFailure:
    case Tpm::kTpmRetryInvalidHandle:
    case Tpm::kTpmRetryLoadFail:
    case Tpm::kTpmRetryLater:
      return CryptoError::CE_TPM_COMM_ERROR;
    case Tpm::kTpmRetryDefendLock:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case Tpm::kTpmRetryReboot:
      return CryptoError::CE_TPM_REBOOT;
    default:
      // TODO(chromium:709646): kTpmRetryFailNoRetry maps here now. Find
      // a better corresponding CryptoError.
      return CryptoError::CE_NONE;
  }
}

bool TpmErrorIsRetriable(Tpm::TpmRetryAction retry_action) {
  return retry_action == Tpm::kTpmRetryLoadFail ||
         retry_action == Tpm::kTpmRetryInvalidHandle ||
         retry_action == Tpm::kTpmRetryCommFailure;
}

}  // namespace

TpmAuthBlock::TpmAuthBlock(Tpm* tpm, TpmInit* tpm_init)
    : tpm_(tpm), tpm_init_(tpm_init) {
  CHECK(tpm != nullptr);
  CHECK(tpm_init_ != nullptr);
}

bool TpmAuthBlock::Derive(const AuthInput& auth_input,
                          const AuthBlockState& state,
                          KeyBlobs* key_out_data,
                          CryptoError* error) {
  ReportEvkkEncryptionType(kTpmBackedEncryption);
  DCHECK(key_out_data);
  CHECK(state.vault_keyset != base::nullopt);

  const SerializedVaultKeyset& serialized = state.vault_keyset.value();
  if (!serialized.has_tpm_key()) {
    LOG(ERROR) << "Decrypting with TPM, but no tpm key present";
    ReportCryptohomeError(kDecryptAttemptButTpmKeyMissing);
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }

  // If the TPM is enabled but not owned, and the keyset is TPM wrapped, then
  // it means the TPM has been cleared since the last login, and is not
  // re-owned.  In this case, the SRK is cleared and we cannot recover the
  // keyset.
  if (tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(ERROR) << "Fatal error--the TPM is enabled but not owned, and this "
               << "keyset was wrapped by the TPM.  It is impossible to "
               << "recover this keyset.";
    ReportCryptohomeError(kDecryptAttemptButTpmNotOwned);
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }

  if (!tpm_init_->HasCryptohomeKey()) {
    tpm_init_->SetupTpm(/*load_key=*/true);
  }

  if (!tpm_init_->HasCryptohomeKey()) {
    LOG(ERROR) << "Vault keyset is wrapped by the TPM, but the TPM is "
               << "unavailable";
    ReportCryptohomeError(kDecryptAttemptButTpmNotAvailable);
    PopulateError(error, CryptoError::CE_TPM_COMM_ERROR);
    return false;
  }

  // This is a sanity check that the keys still match.
  if (serialized.has_tpm_public_key_hash()) {
    if (!IsTPMPubkeyHash(serialized.tpm_public_key_hash(), error)) {
      LOG(ERROR) << "TPM public key hash mismatch.";
      ReportCryptohomeError(kDecryptAttemptButTpmKeyMismatch);
      return false;
    }
  }

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  bool locked_to_single_user =
      auth_input.locked_to_single_user.value_or(false);
  brillo::SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  brillo::SecureBlob tpm_key =
      GetTpmKeyFromSerialized(serialized, locked_to_single_user);
  bool is_pcr_bound = serialized.flags() & SerializedVaultKeyset::PCR_BOUND;
  if (is_pcr_bound) {
    if (!DecryptTpmBoundToPcr(auth_input.user_input.value(), tpm_key, salt,
                              error, &key_out_data->vkk_iv.value(),
                              &key_out_data->vkk_key.value())) {
      return false;
    }
  } else {
    if (!DecryptTpmNotBoundToPcr(
            serialized, auth_input.user_input.value(), tpm_key, salt, error,
            &key_out_data->vkk_iv.value(), &key_out_data->vkk_key.value())) {
      return false;
    }
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

bool TpmAuthBlock::IsTPMPubkeyHash(const std::string& hash,
                                   CryptoError* error) const {
  brillo::SecureBlob pub_key_hash;
  Tpm::TpmRetryAction retry_action =
      tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(), &pub_key_hash);
  if (retry_action == Tpm::kTpmRetryLoadFail ||
      retry_action == Tpm::kTpmRetryInvalidHandle) {
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload key.";
      retry_action = Tpm::kTpmRetryFailNoRetry;
    } else {
      retry_action =
          tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(), &pub_key_hash);
    }
  }
  if (retry_action != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Unable to get the cryptohome public key from the TPM.";
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    PopulateError(error, TpmErrorToCrypto(retry_action));
    return false;
  }
  if ((hash.size() != pub_key_hash.size()) ||
      (brillo::SecureMemcmp(hash.data(), pub_key_hash.data(),
                            pub_key_hash.size()))) {
    PopulateError(error, CryptoError::CE_TPM_FATAL);
    return false;
  }
  return true;
}

brillo::SecureBlob TpmAuthBlock::GetTpmKeyFromSerialized(
    const SerializedVaultKeyset& serialized,
    bool locked_to_single_user) const {
  bool is_pcr_bound = serialized.flags() & SerializedVaultKeyset::PCR_BOUND;
  auto tpm_key_data = (is_pcr_bound && locked_to_single_user)
                          ? serialized.extended_tpm_key()
                          : serialized.tpm_key();
  return brillo::SecureBlob(tpm_key_data);
}

bool TpmAuthBlock::DecryptTpmBoundToPcr(const brillo::SecureBlob& vault_key,
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

    if (!TpmErrorIsRetriable(retry_action))
      break;

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      break;
    }
  }

  LOG(ERROR) << "Failed to unwrap vkk with creds.";
  *error = TpmErrorToCrypto(retry_action);
  return false;
}

bool TpmAuthBlock::DecryptTpmNotBoundToPcr(
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

    if (!TpmErrorIsRetriable(retry_action)) {
      LOG(ERROR) << "Failed to unwrap vkk with creds.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmErrorToCrypto(retry_action);
      return false;
    }

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmErrorToCrypto(Tpm::kTpmRetryFailNoRetry);
      return false;
    }
  }

  if (scrypt_derived) {
    *vkk_key = CryptoLib::HmacSha256(kdf_skey, local_vault_key);
  } else {
    if (!CryptoLib::PasskeyToAesKey(local_vault_key, salt, rounds, vkk_key,
                                    vkk_iv)) {
      LOG(ERROR) << "Failure converting IVKK to VKK";
      PopulateError(error, CryptoError::CE_OTHER_FATAL);
      return false;
    }
  }
  return true;
}

}  // namespace cryptohome
