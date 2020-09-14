// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/vault_keyset.h"

#include <sys/types.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"

using base::FilePath;
using brillo::SecureBlob;

namespace {
const mode_t kVaultFilePermissions = 0600;
}

namespace cryptohome {

VaultKeyset::VaultKeyset()
    : platform_(NULL),
      crypto_(NULL),
      loaded_(false),
      encrypted_(false),
      legacy_index_(-1) {}

VaultKeyset::VaultKeyset(VaultKeyset&& vault_keyset) {
  FromVaultKeyset(vault_keyset);
}

VaultKeyset::~VaultKeyset() {}

void VaultKeyset::Initialize(Platform* platform, Crypto* crypto) {
  platform_ = platform;
  crypto_ = crypto;
}

void VaultKeyset::FromVaultKeyset(const VaultKeyset& vault_keyset) {
  fek_.resize(vault_keyset.fek_.size());
  memcpy(fek_.data(), vault_keyset.fek_.data(), fek_.size());

  fek_sig_.resize(vault_keyset.fek_sig_.size());
  memcpy(fek_sig_.data(), vault_keyset.fek_sig_.data(), fek_sig_.size());

  fek_salt_.resize(vault_keyset.fek_salt_.size());
  memcpy(fek_salt_.data(), vault_keyset.fek_salt_.data(), fek_salt_.size());

  fnek_.resize(vault_keyset.fnek_.size());
  memcpy(fnek_.data(), vault_keyset.fnek_.data(), fnek_.size());

  fnek_sig_.resize(vault_keyset.fnek_sig_.size());
  memcpy(fnek_sig_.data(), vault_keyset.fnek_sig_.data(), fnek_sig_.size());

  fnek_salt_.resize(vault_keyset.fnek_salt_.size());
  memcpy(fnek_salt_.data(), vault_keyset.fnek_salt_.data(), fnek_salt_.size());

  chaps_key_.resize(vault_keyset.chaps_key_.size());
  memcpy(chaps_key_.data(), vault_keyset.chaps_key_.data(), chaps_key_.size());

  reset_seed_.resize(vault_keyset.reset_seed_.size());
  memcpy(reset_seed_.data(), vault_keyset.reset_seed_.data(),
         reset_seed_.size());

  reset_secret_.resize(vault_keyset.reset_secret_.size());
  memcpy(reset_secret_.data(), vault_keyset.reset_secret_.data(),
         reset_secret_.size());
}

void VaultKeyset::FromKeys(const VaultKeysetKeys& keys) {
  fek_.resize(sizeof(keys.fek));
  memcpy(fek_.data(), keys.fek, fek_.size());
  fek_sig_.resize(sizeof(keys.fek_sig));
  memcpy(fek_sig_.data(), keys.fek_sig, fek_sig_.size());
  fek_salt_.resize(sizeof(keys.fek_salt));
  memcpy(fek_salt_.data(), keys.fek_salt, fek_salt_.size());
  fnek_.resize(sizeof(keys.fnek));
  memcpy(fnek_.data(), keys.fnek, fnek_.size());
  fnek_sig_.resize(sizeof(keys.fnek_sig));
  memcpy(fnek_sig_.data(), keys.fnek_sig, fnek_sig_.size());
  fnek_salt_.resize(sizeof(keys.fnek_salt));
  memcpy(fnek_salt_.data(), keys.fnek_salt, fnek_salt_.size());
}

bool VaultKeyset::FromKeysBlob(const SecureBlob& keys_blob) {
  if (keys_blob.size() != sizeof(VaultKeysetKeys)) {
    return false;
  }
  VaultKeysetKeys keys;
  memcpy(&keys, keys_blob.data(), sizeof(keys));

  FromKeys(keys);

  brillo::SecureMemset(&keys, 0, sizeof(keys));
  return true;
}

bool VaultKeyset::ToKeys(VaultKeysetKeys* keys) const {
  brillo::SecureMemset(keys, 0, sizeof(VaultKeysetKeys));
  if (fek_.size() != sizeof(keys->fek)) {
    return false;
  }
  memcpy(keys->fek, fek_.data(), sizeof(keys->fek));
  if (fek_sig_.size() != sizeof(keys->fek_sig)) {
    return false;
  }
  memcpy(keys->fek_sig, fek_sig_.data(), sizeof(keys->fek_sig));
  if (fek_salt_.size() != sizeof(keys->fek_salt)) {
    return false;
  }
  memcpy(keys->fek_salt, fek_salt_.data(), sizeof(keys->fek_salt));
  if (fnek_.size() != sizeof(keys->fnek)) {
    return false;
  }
  memcpy(keys->fnek, fnek_.data(), sizeof(keys->fnek));
  if (fnek_sig_.size() != sizeof(keys->fnek_sig)) {
    return false;
  }
  memcpy(keys->fnek_sig, fnek_sig_.data(), sizeof(keys->fnek_sig));
  if (fnek_salt_.size() != sizeof(keys->fnek_salt)) {
    return false;
  }
  memcpy(keys->fnek_salt, fnek_salt_.data(), sizeof(keys->fnek_salt));

  return true;
}

bool VaultKeyset::ToKeysBlob(SecureBlob* keys_blob) const {
  VaultKeysetKeys keys;
  if (!ToKeys(&keys)) {
    return false;
  }

  SecureBlob local_buffer(sizeof(keys));
  memcpy(local_buffer.data(), &keys, sizeof(keys));
  keys_blob->swap(local_buffer);
  return true;
}

void VaultKeyset::CreateRandomChapsKey() {
  chaps_key_ = CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_CHAPS_KEY_LENGTH);
}

void VaultKeyset::CreateRandomResetSeed() {
  reset_seed_ = CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_RESET_SEED_LENGTH);
}

void VaultKeyset::CreateRandom() {
  CHECK(crypto_);

  fek_ = CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIZE);
  fek_sig_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE);
  fek_salt_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  fnek_ = CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIZE);
  fnek_sig_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SIGNATURE_SIZE);
  fnek_salt_ =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);

  CreateRandomChapsKey();
  CreateRandomResetSeed();
}

const SecureBlob& VaultKeyset::fek() const {
  return fek_;
}

const SecureBlob& VaultKeyset::fek_sig() const {
  return fek_sig_;
}

const SecureBlob& VaultKeyset::fek_salt() const {
  return fek_salt_;
}

const SecureBlob& VaultKeyset::fnek() const {
  return fnek_;
}

const SecureBlob& VaultKeyset::fnek_sig() const {
  return fnek_sig_;
}

const SecureBlob& VaultKeyset::fnek_salt() const {
  return fnek_salt_;
}

void VaultKeyset::set_chaps_key(const SecureBlob& chaps_key) {
  CHECK(chaps_key.size() == CRYPTOHOME_CHAPS_KEY_LENGTH);
  SecureBlob tmp = chaps_key;
  chaps_key_.swap(tmp);
}

void VaultKeyset::clear_chaps_key() {
  CHECK(chaps_key_.size() == CRYPTOHOME_CHAPS_KEY_LENGTH);
  chaps_key_.clear();
  chaps_key_.resize(0);
}

void VaultKeyset::set_reset_seed(const SecureBlob& reset_seed) {
  CHECK_EQ(reset_seed.size(), CRYPTOHOME_RESET_SEED_LENGTH);
  reset_seed_ = reset_seed;
}

void VaultKeyset::set_reset_secret(const SecureBlob& reset_secret) {
  CHECK_EQ(reset_secret.size(), CRYPTOHOME_RESET_SEED_LENGTH);
  reset_secret_ = reset_secret;
}

bool VaultKeyset::Load(const FilePath& filename) {
  CHECK(platform_);
  brillo::Blob contents;
  if (!platform_->ReadFile(filename, &contents))
    return false;
  serialized_.Clear();  // Ensure a fresh start.
  loaded_ = serialized_.ParseFromArray(contents.data(), contents.size());
  // If it was parsed from file, consider it save-able too.
  source_file_.clear();
  if (loaded_) {
    encrypted_ = true;
    source_file_ = filename;
    // For LECredentials, set the key policy appropriately.
    // TODO(crbug.com/832398): get rid of having two ways to identify an
    // LECredential: LE_CREDENTIAL and key_data.policy.low_entropy_credential.
    if (serialized_.flags() & SerializedVaultKeyset::LE_CREDENTIAL) {
      serialized_.mutable_key_data()
          ->mutable_policy()
          ->set_low_entropy_credential(true);
    }
    if (serialized_.has_timestamp_file_exists() &&
        serialized_.timestamp_file_exists()) {
      bool set_from_file_successfully = false;
      FilePath timestamp_path = filename.AddExtension("timestamp");
      brillo::Blob tcontents;
      if (platform_->ReadFile(timestamp_path, &tcontents)) {
        cryptohome::Timestamp timestamp;
        if (timestamp.ParseFromArray(tcontents.data(), tcontents.size())) {
          serialized_.set_last_activity_timestamp(timestamp.timestamp());
          set_from_file_successfully = true;
        } else {
          LOG(WARNING) << "Failure to parse timestamp file: " << timestamp_path;
        }
      } else {
        LOG(WARNING) << "Failure to read timestamp file: " << timestamp_path;
      }

      if (!set_from_file_successfully) {
        // We don't fail the VaultKeyset load here because if it fails, the user
        // may have to recreate their entire cryptohome for this minor error.
        // Instead, we log the error (because it's minor), and let it pass with
        // a reasonable default value for last_activity_timestamp, and that is
        // the current time.
        LOG(WARNING) << "Not failing attempt to Load() due to timestamp file "
                        "problem. Setting last activity timestamp to now";
        serialized_.set_last_activity_timestamp(
            platform_->GetCurrentTime().ToInternalValue());
      }
    }
  }
  return loaded_;
}

bool VaultKeyset::Decrypt(const SecureBlob& key,
                          bool locked_to_single_user,
                          CryptoError* crypto_error) {
  CHECK(crypto_);

  if (crypto_error)
    *crypto_error = CryptoError::CE_NONE;

  if (!loaded_) {
    *crypto_error = CryptoError::CE_OTHER_FATAL;
    return false;
  }

  CryptoError local_crypto_error = CryptoError::CE_NONE;
  bool ok = crypto_->DecryptVaultKeyset(serialized_, key, locked_to_single_user,
                                        nullptr, &local_crypto_error, this);
  if (!ok && local_crypto_error == CryptoError::CE_TPM_COMM_ERROR) {
    ok = crypto_->DecryptVaultKeyset(serialized_, key, locked_to_single_user,
                                     nullptr, &local_crypto_error, this);
  }

  if (!ok && IsLECredential() &&
      local_crypto_error == CryptoError::CE_TPM_DEFEND_LOCK) {
    // For LE credentials, if decrypting the keyset failed due to too many
    // attempts, set auth_locked=true in the keyset. Then save it for future
    // callers who can Load it w/o Decrypt'ing to check that flag.
    serialized_.mutable_key_data()->mutable_policy()->set_auth_locked(true);
    if (!Save(source_file())) {
      LOG(WARNING) << "Failed to set auth_locked in VaultKeyset on disk.";
    }
  }

  // Make sure the returned error is non-empty, because sometimes
  // Crypto::DecryptVaultKeyset() doesn't fill it despite returning false. Note
  // that the value assigned below must *not* say a fatal error, as otherwise
  // this may result in removal of the cryptohome which is undesired in this
  // case.
  if (local_crypto_error == CryptoError::CE_NONE)
    local_crypto_error = CryptoError::CE_OTHER_CRYPTO;

  if (!ok && crypto_error)
    *crypto_error = local_crypto_error;
  return ok;
}

bool VaultKeyset::Encrypt(const SecureBlob& key,
                          const std::string& obfuscated_username) {
  CHECK(crypto_);
  const auto salt =
      CryptoLib::CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  encrypted_ = crypto_->EncryptVaultKeyset(*this, key, salt,
                                           obfuscated_username, &serialized_);
  return encrypted_;
}

bool VaultKeyset::Save(const FilePath& filename) {
  CHECK(platform_);
  if (!encrypted_)
    return false;
  brillo::Blob contents(serialized_.ByteSizeLong());
  google::protobuf::uint8* buf =
      static_cast<google::protobuf::uint8*>(contents.data());
  serialized_.SerializeWithCachedSizesToArray(buf);

  bool ok = platform_->WriteFileAtomicDurable(filename, contents,
                                              kVaultFilePermissions);
  return ok;
}

bool VaultKeyset::IsLECredential() const {
  return serialized_.key_data().policy().low_entropy_credential();
}

bool VaultKeyset::IsSignatureChallengeProtected() const {
  return serialized_.flags() &
         SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED;
}

int VaultKeyset::GetFscryptPolicyVersion() {
  return serialized_.fscrypt_policy_version();
}

void VaultKeyset::SetFscryptPolicyVersion(int policy_version) {
  serialized_.set_fscrypt_policy_version(policy_version);
}

}  // namespace cryptohome
