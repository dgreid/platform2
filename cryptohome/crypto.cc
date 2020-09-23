// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>

#include <crypto/sha2.h>
#include <limits>
#include <map>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

#include "cryptohome/attestation.pb.h"
#include "cryptohome/challenge_credential_auth_block.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/double_wrapped_compat_auth_block.h"
#include "cryptohome/key_objects.h"
#include "cryptohome/le_credential_manager_impl.h"
#include "cryptohome/libscrypt_compat.h"
#include "cryptohome/libscrypt_compat_auth_block.h"
#include "cryptohome/pin_weaver_auth_block.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm_bound_to_pcr_auth_block.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/tpm_not_bound_to_pcr_auth_block.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

// Location where we store the Low Entropy (LE) credential manager related
// state.
const char kSignInHashTreeDir[] = "/home/.shadow/low_entropy_creds";

// Maximum size of the salt file.
const int64_t kSystemSaltMaxSize = (1 << 20);  // 1 MB

// File permissions of salt file (modulo umask).
const mode_t kSaltFilePermissions = 0644;

// This generates the reset secret for PinWeaver credentials. Doing it per
// secret is confusing and difficult to maintain. It's necessary so that
// different credentials can all maintain  the same reset secret (i.e. the
// password resets the PIN), without storing said secret in the clear. In the
// USS key hierarchy, only one reset secret will exist.
bool GenerateResetSecret(const VaultKeyset& vault_keyset,
                         brillo::SecureBlob* reset_secret,
                         brillo::SecureBlob* reset_salt) {
  DCHECK(reset_secret);
  DCHECK(reset_salt);

  // For new users, a reset seed is stored in the VaultKeyset, which is derived
  // into the reset secret.
  if (!vault_keyset.reset_seed().empty()) {
    SecureBlob local_reset_seed(vault_keyset.reset_seed().begin(),
                                vault_keyset.reset_seed().end());
    *reset_salt = CryptoLib::CreateSecureRandomBlob(kAesBlockSize);
    *reset_secret = CryptoLib::HmacSha256(*reset_salt, local_reset_seed);
    return true;
  }

  // When a user credential is being migrated (such as the password), the reset
  // secret needs to remain the same to unlock the PIN. In this case, the reset
  // secret is passed through the vault keyset.
  if (!vault_keyset.reset_secret().empty()) {
    reset_secret->assign(vault_keyset.reset_secret().begin(),
                         vault_keyset.reset_secret().end());
    return true;
  }
  LOG(ERROR) << "The VaultKeyset doesn't have a reset seed, so we can't"
                " set up an LE credential.";
  return false;
}

void DecryptAuthorizationData(const SerializedVaultKeyset& serialized,
                              VaultKeyset* keyset,
                              const SecureBlob& vkk_key,
                              const SecureBlob& vkk_iv) {
  // Use the same key to unwrap the wrapped authorization data.
  if (serialized.key_data().authorization_data_size() > 0) {
    KeyData* key_data = keyset->mutable_serialized()->mutable_key_data();
    for (int auth_data_i = 0; auth_data_i < key_data->authorization_data_size();
         ++auth_data_i) {
      KeyAuthorizationData* auth_data =
          key_data->mutable_authorization_data(auth_data_i);
      for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
        KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
        if (!secret->wrapped() || !secret->has_symmetric_key())
          continue;
        SecureBlob encrypted_auth_key(secret->symmetric_key());
        SecureBlob clear_key;
        // Is it reasonable to use this key here as well?
        if (!CryptoLib::AesDecryptDeprecated(encrypted_auth_key, vkk_key,
                                             vkk_iv, &clear_key)) {
          LOG(ERROR) << "Failed to unwrap a symmetric authorization key:"
                     << " (" << auth_data_i << "," << secret_i << ")";
          // This does not force a failure to use the keyset.
          continue;
        }
        secret->set_symmetric_key(clear_key.to_string());
        secret->set_wrapped(false);
      }
    }
  }
}

bool UnwrapVKKVaultKeyset(const SerializedVaultKeyset& serialized,
                          const KeyBlobs& vkk_data,
                          VaultKeyset* keyset,
                          CryptoError* error) {
  const SecureBlob& vkk_key = vkk_data.vkk_key.value();
  const SecureBlob& vkk_iv = vkk_data.vkk_iv.value();
  const SecureBlob& chaps_iv = vkk_data.chaps_iv.value();
  const SecureBlob& auth_data_iv = vkk_data.authorization_data_iv.value();

  // Decrypt the keyset protobuf.
  SecureBlob local_encrypted_keyset(serialized.wrapped_keyset().begin(),
                                    serialized.wrapped_keyset().end());
  SecureBlob plain_text;

  if (!CryptoLib::AesDecryptDeprecated(local_encrypted_keyset, vkk_key, vkk_iv,
                                       &plain_text)) {
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }
  if (!keyset->FromKeysBlob(plain_text)) {
    LOG(ERROR) << "Failed to decode the keys blob.";
    PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
    return false;
  }

  // Decrypt the chaps key.
  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob local_wrapped_chaps_key(serialized.wrapped_chaps_key());
    SecureBlob unwrapped_chaps_key;

    if (!CryptoLib::AesDecryptDeprecated(local_wrapped_chaps_key, vkk_key,
                                         chaps_iv, &unwrapped_chaps_key)) {
      LOG(ERROR) << "AES decryption failed for chaps key.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    keyset->set_chaps_key(unwrapped_chaps_key);
  }

  // Decrypt the reset seed.
  if (vkk_data.wrapped_reset_seed != base::nullopt &&
      !vkk_data.wrapped_reset_seed.value().empty()) {
    SecureBlob unwrapped_reset_seed;
    SecureBlob local_wrapped_reset_seed =
        SecureBlob(serialized.wrapped_reset_seed());
    SecureBlob local_reset_iv = SecureBlob(serialized.reset_iv());

    if (!CryptoLib::AesDecryptDeprecated(local_wrapped_reset_seed, vkk_key,
                                         local_reset_iv,
                                         &unwrapped_reset_seed)) {
      LOG(ERROR) << "AES decryption failed for reset seed.";
      PopulateError(error, CryptoError::CE_OTHER_CRYPTO);
      return false;
    }

    keyset->set_reset_seed(unwrapped_reset_seed);
  }

  // TODO(kerrnel): Audit if authorization data is used anywhere.
  DecryptAuthorizationData(serialized, keyset, vkk_key, auth_data_iv);

  return true;
}

bool UnwrapScryptVaultKeyset(const SerializedVaultKeyset& serialized,
                             const KeyBlobs& vkk_data,
                             VaultKeyset* keyset,
                             CryptoError* error) {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!LibScryptCompat::Decrypt(blob, vkk_data.scrypt_key.value(),
                                &decrypted)) {
    return false;
  }

  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob chaps_key;
    SecureBlob wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
    if (!LibScryptCompat::Decrypt(
            wrapped_chaps_key, vkk_data.chaps_scrypt_key.value(), &chaps_key)) {
      return false;
    }
    keyset->set_chaps_key(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    SecureBlob reset_seed;
    SecureBlob wrapped_reset_seed = SecureBlob(serialized.wrapped_reset_seed());
    reset_seed.resize(wrapped_reset_seed.size());
    if (!LibScryptCompat::Decrypt(
            wrapped_reset_seed, vkk_data.scrypt_wrapped_reset_seed_key.value(),
            &reset_seed)) {
      return false;
    }
    keyset->set_reset_seed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  // TODO(crbug.com/984782): get rid of this entirely.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  keyset->FromKeysBlob(decrypted);
  return true;
}

}  // namespace

// File name of the system salt file.
const char kSystemSaltFile[] = "salt";

Crypto::Crypto(Platform* platform)
    : use_tpm_(false),
      tpm_(NULL),
      platform_(platform),
      tpm_init_(NULL),
      disable_logging_for_tests_(false) {}

Crypto::~Crypto() {}

bool Crypto::Init(TpmInit* tpm_init) {
  if (use_tpm_) {
    CHECK(tpm_init) << "Crypto wanted to use TPM but was not provided a TPM";
    if (tpm_ == NULL) {
      tpm_ = tpm_init->get_tpm();
    }
    tpm_init_ = tpm_init;
    tpm_init_->SetupTpm(true);
    if (tpm_->GetLECredentialBackend() &&
        tpm_->GetLECredentialBackend()->IsSupported()) {
      le_manager_ = std::make_unique<LECredentialManagerImpl>(
          tpm_->GetLECredentialBackend(), base::FilePath(kSignInHashTreeDir));
    }
  }
  return true;
}

CryptoError Crypto::EnsureTpm(bool reload_key) const {
  CryptoError result = CryptoError::CE_NONE;
  if (tpm_ && tpm_init_) {
    if (reload_key || !tpm_init_->HasCryptohomeKey()) {
      tpm_init_->SetupTpm(true);
    }
  }
  return result;
}

bool Crypto::GetOrCreateSalt(const FilePath& path,
                             size_t length,
                             bool force,
                             SecureBlob* salt) const {
  int64_t file_len = 0;
  if (platform_->FileExists(path)) {
    if (!platform_->GetFileSize(path, &file_len)) {
      LOG(ERROR) << "Can't get file len for " << path.value();
      return false;
    }
  }
  SecureBlob local_salt;
  if (force || file_len == 0 || file_len > kSystemSaltMaxSize) {
    LOG(ERROR) << "Creating new salt at " << path.value() << " (" << force
               << ", " << file_len << ")";
    // If this salt doesn't exist, automatically create it.
    local_salt = CryptoLib::CreateSecureRandomBlob(length);
    if (!platform_->WriteSecureBlobToFileAtomicDurable(path, local_salt,
                                                       kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write user salt";
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform_->ReadFileToSecureBlob(path, &local_salt)) {
      LOG(ERROR) << "Could not read salt file of length " << file_len;
      return false;
    }
  }
  salt->swap(local_salt);
  return true;
}

void Crypto::PasswordToPasskey(const char* password,
                               const brillo::SecureBlob& salt,
                               SecureBlob* passkey) {
  CHECK(password);

  std::string ascii_salt = CryptoLib::SecureBlobToHex(salt);
  // Convert a raw password to a password hash
  SHA256_CTX sha_context;
  SecureBlob md_value(SHA256_DIGEST_LENGTH);

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, ascii_salt.data(), ascii_salt.length());
  SHA256_Update(&sha_context, password, strlen(password));
  SHA256_Final(md_value.data(), &sha_context);

  md_value.resize(SHA256_DIGEST_LENGTH / 2);
  SecureBlob local_passkey(SHA256_DIGEST_LENGTH);
  CryptoLib::SecureBlobToHexToBuffer(md_value, local_passkey.data(),
                                     local_passkey.size());
  passkey->swap(local_passkey);
}

bool Crypto::UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                               const KeyBlobs& vkk_data,
                               VaultKeyset* keyset,
                               CryptoError* error) {
  bool has_vkk_key = vkk_data.vkk_key != base::nullopt &&
                     vkk_data.vkk_iv != base::nullopt &&
                     vkk_data.chaps_iv != base::nullopt &&
                     vkk_data.authorization_data_iv != base::nullopt;
  bool has_scrypt_key = vkk_data.scrypt_key != base::nullopt;
  bool successfully_unwrapped = false;

  if (has_vkk_key && !has_scrypt_key) {
    successfully_unwrapped =
        UnwrapVKKVaultKeyset(serialized, vkk_data, keyset, error);
  } else if (has_scrypt_key && !has_vkk_key) {
    successfully_unwrapped =
        UnwrapScryptVaultKeyset(serialized, vkk_data, keyset, error);
  } else {
    DLOG(FATAL) << "An invalid key combination exists";
    return false;
  }

  if (successfully_unwrapped) {
    // By this point we know that the TPM is successfully owned, everything
    // is initialized, and we were able to successfully decrypt a
    // TPM-wrapped keyset. So, for TPMs with updateable firmware, we assume
    // that it is stable (and the TPM can invalidate the old version).
    // TODO(dlunev): We shall try to get this out of cryptohome eventually.
    const bool tpm_backed =
        (serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED) ||
        (serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL);
    if (use_tpm_ && tpm_backed && tpm_ != nullptr) {
      tpm_->DeclareTpmFirmwareStable();
    }
  }
  return successfully_unwrapped;
}

bool Crypto::DecryptScrypt(const SerializedVaultKeyset& serialized,
                           const SecureBlob& key,
                           CryptoError* error,
                           VaultKeyset* keyset) const {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!CryptoLib::DecryptScryptBlob(blob, key, &decrypted, error)) {
    LOG(ERROR) << "Wrapped keyset Scrypt decrypt failed.";
    return false;
  }

  bool chaps_key_present = serialized.has_wrapped_chaps_key();
  if (chaps_key_present) {
    SecureBlob chaps_key;
    SecureBlob wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
    // Perform a Scrypt operation on wrapped chaps key.
    if (!CryptoLib::DecryptScryptBlob(wrapped_chaps_key, key, &chaps_key,
                                      error)) {
      LOG(ERROR) << "Chaps key scrypt decrypt failed.";
      return false;
    }
    keyset->set_chaps_key(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    SecureBlob reset_seed;
    SecureBlob wrapped_reset_seed = SecureBlob(serialized.wrapped_reset_seed());
    reset_seed.resize(wrapped_reset_seed.size());
    // Perform a Scrypt operation on wrapped reset seed.
    if (!CryptoLib::DecryptScryptBlob(wrapped_reset_seed, key, &reset_seed,
                                      error)) {
      LOG(ERROR) << "Reset seed scrypt decrypt failed.";
      return false;
    }
    keyset->set_reset_seed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  // TODO(crbug.com/984782): get rid of this entirely.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  keyset->FromKeysBlob(decrypted);
  return true;
}

bool Crypto::NeedsPcrBinding(const uint64_t& label) const {
  DCHECK(le_manager_)
      << "le_manage_ doesn't exist when calling NeedsPcrBinding()";
  return le_manager_->NeedsPcrBinding(label);
}

bool Crypto::DecryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                const SecureBlob& vault_key,
                                bool locked_to_single_user,
                                unsigned int* crypt_flags,
                                CryptoError* error,
                                VaultKeyset* vault_keyset) {
  if (crypt_flags)
    *crypt_flags = serialized.flags();
  PopulateError(error, CryptoError::CE_NONE);

  unsigned int flags = serialized.flags();

  if (flags & SerializedVaultKeyset::LE_CREDENTIAL) {
    PinWeaverAuthBlock pin_weaver_auth(le_manager_.get(), tpm_init_);

    AuthInput auth_input = {vault_key};
    AuthBlockState auth_state = {serialized};
    KeyBlobs vkk_data;
    if (!pin_weaver_auth.Derive(auth_input, auth_state, &vkk_data, error)) {
      return false;
    }

    // This is possible to be empty if an old version of CR50 is running.
    if (vkk_data.reset_secret.has_value() &&
        !vkk_data.reset_secret.value().empty()) {
      vault_keyset->set_reset_secret(vkk_data.reset_secret.value());
    }

    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  if (flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) {
    AuthInput user_input = {vault_key};
    AuthBlockState auth_state = {serialized};
    KeyBlobs vkk_data;
    ChallengeCredentialAuthBlock auth_block;
    if (!auth_block.Derive(user_input, auth_state, &vkk_data, error)) {
      return false;
    }
    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  if (flags & SerializedVaultKeyset::SCRYPT_WRAPPED &&
      flags & SerializedVaultKeyset::TPM_WRAPPED) {
    LOG(ERROR) << "Keyset wrapped with both TPM and Scrypt?";
    ReportCryptohomeError(cryptohome::kBothTpmAndScryptWrappedKeyset);

    AuthInput auth_input = {vault_key};
    AuthBlockState auth_state = {serialized};
    KeyBlobs vkk_data;
    DoubleWrappedCompatAuthBlock auth_block(tpm_, tpm_init_);
    if (!auth_block.Derive(auth_input, auth_state, &vkk_data, error)) {
      return false;
    }
    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  if (flags & SerializedVaultKeyset::TPM_WRAPPED) {
    std::unique_ptr<AuthBlock> tpm_auth;
    if (flags & SerializedVaultKeyset::PCR_BOUND) {
      tpm_auth = std::make_unique<TpmBoundToPcrAuthBlock>(tpm_, tpm_init_);
    } else {
      tpm_auth = std::make_unique<TpmNotBoundToPcrAuthBlock>(tpm_, tpm_init_);
    }

    KeyBlobs vkk_data;
    AuthInput auth_input;
    auth_input.user_input = vault_key;
    auth_input.locked_to_single_user = locked_to_single_user;

    AuthBlockState auth_state = {serialized};
    if (!tpm_auth->Derive(auth_input, auth_state, &vkk_data, error)) {
      return false;
    }

    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  if (flags & SerializedVaultKeyset::SCRYPT_WRAPPED) {
    KeyBlobs vkk_data;
    AuthInput auth_input;
    auth_input.user_input = vault_key;
    auth_input.locked_to_single_user = locked_to_single_user;

    AuthBlockState auth_state = {serialized};
    LibScryptCompatAuthBlock auth_block;
    if (!auth_block.Derive(auth_input, auth_state, &vkk_data, error)) {
      return false;
    }

    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  LOG(ERROR) << "Keyset wrapped with unknown method.";
  return false;
}

bool Crypto::GenerateEncryptedRawKeyset(const VaultKeyset& vault_keyset,
                                        const SecureBlob& vkk_key,
                                        const SecureBlob& fek_iv,
                                        const SecureBlob& chaps_iv,
                                        SecureBlob* cipher_text,
                                        SecureBlob* wrapped_chaps_key) const {
  SecureBlob blob;
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }

  SecureBlob chaps_key = vault_keyset.chaps_key();
  if (!CryptoLib::AesEncryptDeprecated(blob, vkk_key, fek_iv, cipher_text) ||
      !CryptoLib::AesEncryptDeprecated(chaps_key, vkk_key, chaps_iv,
                                       wrapped_chaps_key)) {
    LOG(ERROR) << "AES encryption failed.";
    return false;
  }

  return true;
}

bool Crypto::GenerateAndWrapKeys(const VaultKeyset& vault_keyset,
                                 const SecureBlob& key,
                                 const SecureBlob& salt,
                                 const KeyBlobs& blobs,
                                 bool store_reset_seed,
                                 SerializedVaultKeyset* serialized) const {
  if (blobs.vkk_key == base::nullopt || blobs.vkk_iv == base::nullopt ||
      blobs.chaps_iv == base::nullopt) {
    DLOG(FATAL) << "Fields missing from KeyBlobs.";
    return false;
  }

  SecureBlob cipher_text;
  SecureBlob wrapped_chaps_key;
  if (!GenerateEncryptedRawKeyset(vault_keyset, blobs.vkk_key.value(),
                                  blobs.vkk_iv.value(), blobs.chaps_iv.value(),
                                  &cipher_text, &wrapped_chaps_key)) {
    return false;
  }

  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());

  // If a reset seed is present, encrypt and store it, else clear the field.
  if (store_reset_seed && vault_keyset.reset_seed().size() != 0) {
    const auto reset_iv = CryptoLib::CreateSecureRandomBlob(kAesBlockSize);
    SecureBlob wrapped_reset_seed;
    if (!CryptoLib::AesEncryptDeprecated(vault_keyset.reset_seed(),
                                         blobs.vkk_key.value(), reset_iv,
                                         &wrapped_reset_seed)) {
      LOG(ERROR) << "AES encryption of Reset seed failed.";
      return false;
    }
    serialized->set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                       wrapped_reset_seed.size());
    serialized->set_reset_iv(reset_iv.data(), reset_iv.size());
  } else {
    serialized->clear_wrapped_reset_seed();
    serialized->clear_reset_iv();
  }

  return true;
}

bool Crypto::EncryptTPM(const VaultKeyset& vault_keyset,
                        const SecureBlob& key,
                        const SecureBlob& salt,
                        const std::string& obfuscated_username,
                        KeyBlobs* out_blobs,
                        SerializedVaultKeyset* serialized) const {
  if (!use_tpm_)
    return false;
  EnsureTpm(false);
  if (!is_cryptohome_key_loaded())
    return false;

  const auto vkk_key = CryptoLib::CreateSecureRandomBlob(kDefaultAesKeySize);
  SecureBlob pass_blob(kDefaultPassBlobSize);
  SecureBlob vkk_iv(kAesBlockSize);
  if (!CryptoLib::DeriveSecretsScrypt(key, salt, {&pass_blob, &vkk_iv}))
    return false;

  SecureBlob tpm_key;
  SecureBlob extended_tpm_key;
  std::map<uint32_t, std::string> default_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, std::string> extended_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

  // Encrypt the VKK using the TPM and the user's passkey. The output is two
  // encrypted blobs, sealed to PCR in |tpm_key| and |extended_tpm_key|,
  // which are stored in the serialized vault keyset.
  if (tpm_->SealToPcrWithAuthorization(tpm_init_->GetCryptohomeKey(), vkk_key,
                                       pass_blob, default_pcr_map,
                                       &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return false;
  }
  if (tpm_->SealToPcrWithAuthorization(
          tpm_init_->GetCryptohomeKey(), vkk_key, pass_blob, extended_pcr_map,
          &extended_tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds for extended PCR.";
    return false;
  }

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(), &pub_key_hash) ==
      Tpm::kTpmRetryNone)
    serialized->set_tpm_public_key_hash(pub_key_hash.data(),
                                        pub_key_hash.size());

  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::SCRYPT_WRAPPED) |
                        SerializedVaultKeyset::TPM_WRAPPED |
                        SerializedVaultKeyset::SCRYPT_DERIVED |
                        SerializedVaultKeyset::PCR_BOUND);
  serialized->set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized->set_extended_tpm_key(extended_tpm_key.data(),
                                   extended_tpm_key.size());

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  out_blobs->vkk_key = vkk_key;
  out_blobs->vkk_iv = vkk_iv;
  out_blobs->chaps_iv = vkk_iv;
  out_blobs->auth_iv = vkk_iv;

  return true;
}

bool Crypto::EncryptTPMNotBoundToPcr(const VaultKeyset& vault_keyset,
                                     const SecureBlob& key,
                                     const SecureBlob& salt,
                                     KeyBlobs* out_blobs,
                                     SerializedVaultKeyset* serialized) const {
  if (!use_tpm_)
    return false;
  EnsureTpm(false);
  if (!is_cryptohome_key_loaded())
    return false;
  const auto local_blob = CryptoLib::CreateSecureRandomBlob(kDefaultAesKeySize);
  SecureBlob tpm_key;
  SecureBlob aes_skey(kDefaultAesKeySize);
  SecureBlob kdf_skey(kDefaultAesKeySize);
  SecureBlob vkk_iv(kAesBlockSize);

  if (!CryptoLib::DeriveSecretsScrypt(key, salt,
                                      {&aes_skey, &kdf_skey, &vkk_iv})) {
    return false;
  }
  // Encrypt the VKK using the TPM and the user's passkey.  The output is an
  // encrypted blob in tpm_key, which is stored in the serialized vault
  // keyset.
  if (tpm_->EncryptBlob(tpm_init_->GetCryptohomeKey(), local_blob, aes_skey,
                        &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return false;
  }

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(), &pub_key_hash) ==
      Tpm::kTpmRetryNone)
    serialized->set_tpm_public_key_hash(pub_key_hash.data(),
                                        pub_key_hash.size());

  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::SCRYPT_WRAPPED &
                         ~SerializedVaultKeyset::PCR_BOUND) |
                        SerializedVaultKeyset::TPM_WRAPPED |
                        SerializedVaultKeyset::SCRYPT_DERIVED);
  serialized->set_tpm_key(tpm_key.data(), tpm_key.size());

  SecureBlob vkk_key = CryptoLib::HmacSha256(kdf_skey, local_blob);

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  out_blobs->vkk_key = vkk_key;
  out_blobs->vkk_iv = vkk_iv;
  out_blobs->chaps_iv = vkk_iv;
  out_blobs->auth_iv = vkk_iv;

  return true;
}

bool Crypto::EncryptScrypt(const VaultKeyset& vault_keyset,
                           const SecureBlob& key,
                           SerializedVaultKeyset* serialized) const {
  SecureBlob blob;
  if (vault_keyset.IsLECredential()) {
    LOG(ERROR) << "Low entropy credentials cannot be scrypt-wrapped.";
    return false;
  }
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }
  // Append the SHA1 hash of the keyset blob. This is done solely for
  // backwards-compatibility purposes, since scrypt already creates a
  // MAC for the encrypted blob. It is ignored in DecryptScrypt since
  // it is redundant.
  SecureBlob hash = CryptoLib::Sha1(blob);
  SecureBlob local_blob = SecureBlob::Combine(blob, hash);
  SecureBlob cipher_text;

  if (!CryptoLib::EncryptScryptBlob(local_blob, key, &cipher_text)) {
    LOG(ERROR) << "Scrypt encrypt of keyset blob failed.";
    return false;
  }

  SecureBlob wrapped_chaps_key;
  if (!CryptoLib::EncryptScryptBlob(vault_keyset.chaps_key(), key,
                                    &wrapped_chaps_key)) {
    LOG(ERROR) << "Scrypt encrypt of chaps key failed.";
    return false;
  }
  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::TPM_WRAPPED) |
                        SerializedVaultKeyset::SCRYPT_WRAPPED);
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  // If there is a reset seed, encrypt and store it.
  if (vault_keyset.reset_seed().size() != 0) {
    SecureBlob wrapped_reset_seed;
    if (!CryptoLib::EncryptScryptBlob(vault_keyset.reset_seed(), key,
                                      &wrapped_reset_seed)) {
      LOG(ERROR) << "Scrypt encrypt of reset seed failed.";
      return false;
    }
    serialized->set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                       wrapped_reset_seed.size());
  } else {
    serialized->clear_wrapped_reset_seed();
  }

  return true;
}

bool Crypto::EncryptChallengeCredential(
    const VaultKeyset& vault_keyset,
    const SecureBlob& key,
    const std::string& obfuscated_username,
    SerializedVaultKeyset* serialized) const {
  serialized->set_flags(serialized->flags() |
                        SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  if (!EncryptScrypt(vault_keyset, key, serialized))
    return false;
  DCHECK(!(serialized->flags() & SerializedVaultKeyset::TPM_WRAPPED));
  DCHECK(serialized->flags() & SerializedVaultKeyset::SCRYPT_WRAPPED);
  DCHECK(serialized->flags() &
         SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  return true;
}

bool Crypto::EncryptAuthorizationData(SerializedVaultKeyset* serialized,
                                      const SecureBlob& vkk_key,
                                      const SecureBlob& vkk_iv) const {
  if (serialized->key_data().authorization_data_size() <= 0)
    return true;

  // Handle AuthorizationData secrets if provided.
  KeyData* key_data = serialized->mutable_key_data();
  for (int auth_data_i = 0; auth_data_i < key_data->authorization_data_size();
       ++auth_data_i) {
    KeyAuthorizationData* auth_data =
        key_data->mutable_authorization_data(auth_data_i);
    for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
      KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
      // Secrets that are externally provided should not be wrapped when
      // this is called.  However, calling Encrypt() again should be
      // idempotent.  External callers should be filtered at the API layer.
      if (secret->wrapped() || !secret->has_symmetric_key())
        continue;
      SecureBlob clear_auth_key(secret->symmetric_key());
      SecureBlob encrypted_auth_key;

      if (!CryptoLib::AesEncryptDeprecated(clear_auth_key, vkk_key, vkk_iv,
                                           &encrypted_auth_key)) {
        LOG(ERROR) << "Failed to wrap a symmetric authorization key:"
                   << " (" << auth_data_i << "," << secret_i << ")";
        // This forces a failure.
        return false;
      }
      secret->set_symmetric_key(encrypted_auth_key.to_string());
      secret->set_wrapped(true);
    }
  }

  return true;
}

bool Crypto::EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                const SecureBlob& vault_key,
                                const SecureBlob& vault_key_salt,
                                const std::string& obfuscated_username,
                                SerializedVaultKeyset* serialized) const {
  if (vault_keyset.IsLECredential()) {
    SecureBlob reset_secret;
    SecureBlob reset_salt;
    if (!GenerateResetSecret(vault_keyset, &reset_secret, &reset_salt)) {
      return false;
    }

    serialized->set_reset_salt(reset_salt.data(), reset_salt.size());

    KeyBlobs blobs;
    PinWeaverAuthBlock pin_weaver_auth(le_manager_.get(), tpm_init_);

    AuthInput user_input = {vault_key, /*is_pcr_extended=*/base::nullopt,
                            vault_key_salt, obfuscated_username, reset_secret};
    KeyBlobs vkk_data;
    CryptoError error;

    // TODO(kerrnel): When switching to a factory method, report the error
    // object.
    auto auth_state = pin_weaver_auth.Create(user_input, &vkk_data, &error);
    if (auth_state == base::nullopt) {
      LOG(ERROR) << "Failed to create pinweaver credential: " << error;
      return false;
    }

    const AuthBlockState& state = auth_state.value();
    serialized->set_le_fek_iv(state.vault_keyset.value().le_fek_iv());
    serialized->set_le_chaps_iv(state.vault_keyset.value().le_chaps_iv());
    serialized->set_flags(state.vault_keyset.value().flags());
    serialized->set_le_label(state.vault_keyset.value().le_label());
    serialized->mutable_key_data()->mutable_policy()->set_auth_locked(false);

    if (!GenerateAndWrapKeys(vault_keyset, vault_key, vault_key_salt, vkk_data,
                             /*store_reset_seed=*/false, serialized)) {
      LOG(ERROR) << "Failed to generate unwrapped keys";
      return false;
    }

    if (!EncryptAuthorizationData(serialized, vkk_data.vkk_key.value(),
                                  vkk_data.auth_iv.value())) {
      return false;
    }
  } else if (vault_keyset.IsSignatureChallengeProtected()) {
    if (!EncryptChallengeCredential(vault_keyset, vault_key,
                                    obfuscated_username, serialized)) {
      // TODO(crbug.com/842791): add ReportCryptohomeError
      return false;
    }
  } else {
    bool encrypt_tpm_success = false;
    KeyBlobs blobs;
    if (CanUnsealWithUserAuth()) {
      encrypt_tpm_success = EncryptTPM(vault_keyset, vault_key, vault_key_salt,
                                       obfuscated_username, &blobs, serialized);
    } else {
      encrypt_tpm_success = EncryptTPMNotBoundToPcr(
          vault_keyset, vault_key, vault_key_salt, &blobs, serialized);
    }
    if (!encrypt_tpm_success) {
      LOG_IF(ERROR, !disable_logging_for_tests_) << "Encrypt using TPM failed";
      if (use_tpm_ && tpm_ && tpm_->IsOwned()) {
        ReportCryptohomeError(kEncryptWithTpmFailed);
      }
      if (!EncryptScrypt(vault_keyset, vault_key, serialized)) {
        return false;
      }
    } else {
      if (!GenerateAndWrapKeys(vault_keyset, vault_key, vault_key_salt, blobs,
                               /*store_reset_seed=*/true, serialized)) {
        LOG(ERROR) << "Failed to generate unwrapped keys";
        return false;
      }

      if (!EncryptAuthorizationData(serialized, blobs.vkk_key.value(),
                                    blobs.auth_iv.value())) {
        return false;
      }
    }
  }

  serialized->set_salt(vault_key_salt.data(), vault_key_salt.size());
  return true;
}

bool Crypto::EncryptWithTpm(const SecureBlob& data,
                            std::string* encrypted_data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!CreateSealedKey(&aes_key, &sealed_key))
    return false;
  return EncryptData(data, aes_key, sealed_key, encrypted_data);
}

bool Crypto::DecryptWithTpm(const std::string& encrypted_data,
                            SecureBlob* data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!UnsealKey(encrypted_data, &aes_key, &sealed_key)) {
    return false;
  }
  return DecryptData(encrypted_data, aes_key, data);
}

bool Crypto::CreateSealedKey(SecureBlob* aes_key,
                             SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  if (!tpm_->GetRandomDataSecureBlob(kDefaultAesKeySize, aes_key)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }
  if (!tpm_->SealToPCR0(*aes_key, sealed_key)) {
    LOG(ERROR) << "Failed to seal cipher key.";
    return false;
  }
  return true;
}

bool Crypto::EncryptData(const SecureBlob& data,
                         const SecureBlob& aes_key,
                         const SecureBlob& sealed_key,
                         std::string* encrypted_data) const {
  if (!use_tpm_)
    return false;
  SecureBlob iv;
  if (!tpm_->GetRandomDataSecureBlob(kAesBlockSize, &iv)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob encrypted_data_blob;
  if (!CryptoLib::AesEncryptSpecifyBlockMode(
          data, 0, data.size(), aes_key, iv, CryptoLib::kPaddingStandard,
          CryptoLib::kCbc, &encrypted_data_blob)) {
    LOG(ERROR) << "Failed to encrypt serial data.";
    return false;
  }
  EncryptedData encrypted_pb;
  encrypted_pb.set_wrapped_key(sealed_key.data(), sealed_key.size());
  encrypted_pb.set_iv(iv.data(), iv.size());
  encrypted_pb.set_encrypted_data(encrypted_data_blob.data(),
                                  encrypted_data_blob.size());
  encrypted_pb.set_mac(
      CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb, aes_key));
  if (!encrypted_pb.SerializeToString(encrypted_data)) {
    LOG(ERROR) << "Could not serialize data to string.";
    return false;
  }
  return true;
}

bool Crypto::UnsealKey(const std::string& encrypted_data,
                       SecureBlob* aes_key,
                       SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  SecureBlob tmp(encrypted_pb.wrapped_key().begin(),
                 encrypted_pb.wrapped_key().end());
  sealed_key->swap(tmp);
  if (!tpm_->Unseal(*sealed_key, aes_key)) {
    LOG(ERROR) << "Cannot unseal aes key.";
    return false;
  }
  return true;
}

bool Crypto::DecryptData(const std::string& encrypted_data,
                         const brillo::SecureBlob& aes_key,
                         brillo::SecureBlob* data) const {
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  std::string mac = CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb, aes_key);
  if (mac.length() != encrypted_pb.mac().length()) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  if (0 != brillo::SecureMemcmp(mac.data(), encrypted_pb.mac().data(),
                                mac.length())) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  SecureBlob iv(encrypted_pb.iv().begin(), encrypted_pb.iv().end());
  SecureBlob encrypted_data_blob(encrypted_pb.encrypted_data().begin(),
                                 encrypted_pb.encrypted_data().end());
  if (!CryptoLib::AesDecryptSpecifyBlockMode(
          encrypted_data_blob, 0, encrypted_data_blob.size(), aes_key, iv,
          CryptoLib::kPaddingStandard, CryptoLib::kCbc, data)) {
    LOG(ERROR) << "Failed to decrypt encrypted data.";
    return false;
  }
  return true;
}

bool Crypto::ResetLECredential(const SerializedVaultKeyset& serialized_reset,
                               CryptoError* error,
                               const VaultKeyset& vk) const {
  if (!use_tpm_ || !tpm_)
    return false;

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    LOG(ERROR) << "Attempting to Reset LECredential on a platform that doesn't "
                  "support LECredential";
    PopulateError(error, CryptoError::CE_LE_NOT_SUPPORTED);
    return false;
  }

  CHECK(serialized_reset.flags() & SerializedVaultKeyset::LE_CREDENTIAL);
  SecureBlob local_reset_seed(vk.reset_seed().begin(), vk.reset_seed().end());
  SecureBlob reset_salt(serialized_reset.reset_salt().begin(),
                        serialized_reset.reset_salt().end());
  if (local_reset_seed.empty() || reset_salt.empty()) {
    LOG(ERROR) << "Reset seed/salt is empty, can't reset LE credential.";
    PopulateError(error, CryptoError::CE_OTHER_FATAL);
    return false;
  }

  SecureBlob reset_secret = CryptoLib::HmacSha256(reset_salt, local_reset_seed);
  int ret =
      le_manager_->ResetCredential(serialized_reset.le_label(), reset_secret);
  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ret == LE_CRED_ERROR_INVALID_RESET_SECRET
                             ? CryptoError::CE_LE_INVALID_SECRET
                             : CryptoError::CE_OTHER_FATAL);
    return false;
  }
  return true;
}

int Crypto::GetWrongAuthAttempts(
    const SerializedVaultKeyset& le_serialized) const {
  DCHECK(le_manager_)
      << "le_manage_ doesn't exist when calling GetWrongAuthAttempts()";
  return le_manager_->GetWrongAuthAttempts(le_serialized.le_label());
}

bool Crypto::RemoveLECredential(uint64_t label) const {
  if (!use_tpm_ || !tpm_) {
    LOG(WARNING) << "No TPM instance for RemoveLECredential.";
    return false;
  }

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    LOG(ERROR) << "No LECredentialManager instance for RemoveLECredential.";
    return false;
  }

  return le_manager_->RemoveCredential(label) == LE_CRED_SUCCESS;
}

bool Crypto::is_cryptohome_key_loaded() const {
  if (tpm_ == NULL || tpm_init_ == NULL) {
    return false;
  }
  return tpm_init_->HasCryptohomeKey();
}

bool Crypto::CanUnsealWithUserAuth() const {
  if (!tpm_)
    return false;
  if (tpm_->GetVersion() != Tpm::TPM_1_2)
    return true;
  if (!tpm_->DelegateCanResetDACounter())
    return false;
  base::Optional<bool> is_pcr_bound = tpm_->IsDelegateBoundToPcr();
  if (is_pcr_bound.has_value() && !is_pcr_bound.value())
    return true;

#if USE_DOUBLE_EXTEND_PCR_ISSUE
  return false;
#else
  return true;
#endif
}

}  // namespace cryptohome
