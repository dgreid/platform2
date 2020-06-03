// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/pin_weaver_auth_block.h"

#include "cryptohome/cryptolib.h"

#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "vault_keyset.pb.h"  // NOLINT(build/include)

namespace cryptohome {

namespace {

CryptoError ConvertLeError(int le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_INVALID_LE_SECRET:
      return CryptoError::CE_LE_INVALID_SECRET;
    case LE_CRED_ERROR_TOO_MANY_ATTEMPTS:
      return CryptoError::CE_TPM_DEFEND_LOCK;
    case LE_CRED_ERROR_INVALID_LABEL:
      return CryptoError::CE_OTHER_FATAL;
    case LE_CRED_ERROR_HASH_TREE:
      return CryptoError::CE_OTHER_FATAL;
    case LE_CRED_ERROR_PCR_NOT_MATCH:
      // We might want to return an error here that will make the device
      // reboot.
      LOG(ERROR) << "PCR in unexpected state.";
      return CryptoError::CE_LE_INVALID_SECRET;
    default:
      return CryptoError::CE_OTHER_FATAL;
  }
}

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
const char kHESecretHmacData[] = "vkk_seed";

}  // namespace

PinWeaverAuthBlock::PinWeaverAuthBlock(LECredentialManager* le_manager)
    : le_manager_(le_manager) {
  CHECK(le_manager != nullptr);
}

bool PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                const AuthBlockState& state,
                                KeyBlobs* key_blobs,
                                CryptoError* error) {
  DCHECK(key_blobs);
  DCHECK(state.vault_keyset != base::nullopt);
  const SerializedVaultKeyset& serialized = state.vault_keyset.value();

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    PopulateError(error, CryptoError::CE_LE_NOT_SUPPORTED);
    return false;
  }

  CHECK(serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL);

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  brillo::SecureBlob le_iv(kAesBlockSize);
  brillo::SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  if (!CryptoLib::DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                                      {&le_secret, &kdf_skey, &le_iv})) {
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }

  key_blobs->reset_secret = brillo::SecureBlob();
  key_blobs->authorization_data_iv = le_iv;
  key_blobs->chaps_iv = brillo::SecureBlob(serialized.le_chaps_iv().begin(),
                                           serialized.le_chaps_iv().end());

  // Try to obtain the HE Secret from the LECredentialManager.
  brillo::SecureBlob he_secret;
  int ret =
      le_manager_->CheckCredential(serialized.le_label(), le_secret, &he_secret,
                                   &key_blobs->reset_secret.value());

  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ConvertLeError(ret));
    return false;
  }

  key_blobs->vkk_iv = brillo::SecureBlob(serialized.le_fek_iv().begin(),
                                         serialized.le_fek_iv().end());

  brillo::SecureBlob vkk_seed = CryptoLib::HmacSha256(
      he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = CryptoLib::HmacSha256(kdf_skey, vkk_seed);

  return true;
}

}  // namespace cryptohome
