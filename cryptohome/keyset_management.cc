// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/keyset_management.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <dbus/cryptohome/dbus-constants.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"

#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset_factory.h"

namespace cryptohome {

KeysetManagement::KeysetManagement(
    Platform* platform,
    Crypto* crypto,
    const brillo::SecureBlob& system_salt,
    std::unique_ptr<VaultKeysetFactory> vault_keyset_factory)
    : platform_(platform),
      crypto_(crypto),
      system_salt_(system_salt),
      vault_keyset_factory_(std::move(vault_keyset_factory)) {}

bool KeysetManagement::AreCredentialsValid(const Credentials& creds) {
  std::unique_ptr<VaultKeyset> vk = GetValidKeyset(creds, nullptr /* error */);
  return vk.get() != nullptr;
}

std::unique_ptr<VaultKeyset> KeysetManagement::GetValidKeyset(
    const Credentials& creds, MountError* error) {
  if (error)
    *error = MOUNT_ERROR_NONE;

  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);

  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    if (error)
      *error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
    return nullptr;
  }

  bool any_keyset_exists = false;
  CryptoError last_crypto_error = CryptoError::CE_NONE;
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
    if (!vk) {
      continue;
    }
    any_keyset_exists = true;
    // Skip decrypt attempts if the label doesn't match.
    // Treat an empty creds label as a wildcard.
    if (!creds.key_data().label().empty() &&
        creds.key_data().label() != vk->label())
      continue;
    // Skip LE Credentials if not explicitly identified by a label, since we
    // don't want unnecessary wrong attempts.
    if (creds.key_data().label().empty() &&
        (vk->serialized().flags() & SerializedVaultKeyset::LE_CREDENTIAL))
      continue;
    bool locked_to_single_user =
        platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
    if (vk->Decrypt(creds.passkey(), locked_to_single_user,
                    &last_crypto_error)) {
      return vk;
    }
  }

  MountError local_error = MOUNT_ERROR_NONE;
  if (!any_keyset_exists) {
    LOG(ERROR) << "No parsable keysets found for " << obfuscated;
    local_error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
  } else if (last_crypto_error == CryptoError::CE_NONE) {
    // If we're searching by label, don't let a no-key-found become
    // MOUNT_ERROR_FATAL.  In the past, no parseable key was a fatal
    // error.  Just treat it like an invalid key.  This allows for
    // multiple per-label requests then a wildcard, worst case, before
    // the Cryptohome is removed.
    if (!creds.key_data().label().empty()) {
      LOG(ERROR) << "Failed to find the specified keyset for " << obfuscated;
      local_error = MOUNT_ERROR_KEY_FAILURE;
    } else {
      LOG(ERROR) << "Failed to find any suitable keyset for " << obfuscated;
      local_error = MOUNT_ERROR_FATAL;
    }
  } else {
    switch (last_crypto_error) {
      case CryptoError::CE_TPM_FATAL:
      case CryptoError::CE_OTHER_FATAL:
        local_error = MOUNT_ERROR_VAULT_UNRECOVERABLE;
        break;
      case CryptoError::CE_TPM_COMM_ERROR:
        local_error = MOUNT_ERROR_TPM_COMM_ERROR;
        break;
      case CryptoError::CE_TPM_DEFEND_LOCK:
        local_error = MOUNT_ERROR_TPM_DEFEND_LOCK;
        break;
      case CryptoError::CE_TPM_REBOOT:
        local_error = MOUNT_ERROR_TPM_NEEDS_REBOOT;
        break;
      default:
        local_error = MOUNT_ERROR_KEY_FAILURE;
        break;
    }
    LOG(ERROR) << "Failed to decrypt any keysets for " << obfuscated
               << ": mount error " << local_error << ", crypto error "
               << last_crypto_error;
  }
  if (error)
    *error = local_error;
  return nullptr;
}

std::unique_ptr<VaultKeyset> KeysetManagement::GetVaultKeyset(
    const std::string& obfuscated_username,
    const std::string& key_label) const {
  if (key_label.empty())
    return NULL;

  // Walk all indices to find a match.
  // We should move to label-derived suffixes to be efficient.
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices)) {
    return NULL;
  }
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    if (vk->label() == key_label) {
      return vk;
    }
  }
  return NULL;
}

// TODO(wad) Figure out how this might fit in with vault_keyset.cc
bool KeysetManagement::GetVaultKeysets(const std::string& obfuscated,
                                       std::vector<int>* keysets) const {
  CHECK(keysets);
  base::FilePath user_dir = ShadowRoot().Append(obfuscated);

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false, base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    base::FilePath file_name = next_path.BaseName();
    // Scan for "master." files.
    if (file_name.RemoveFinalExtension().value() != kKeyFile) {
      continue;
    }
    std::string index_str = file_name.FinalExtension();
    int index;
    if (!base::StringToInt(&index_str[1], &index)) {
      continue;
    }
    // The test below will catch all strtol(3) error conditions.
    if (index < 0 || index >= kKeyFileMax) {
      LOG(ERROR) << "Invalid key file range: " << index;
      continue;
    }
    keysets->push_back(static_cast<int>(index));
  }

  // Ensure it is sorted numerically and not lexigraphically.
  std::sort(keysets->begin(), keysets->end());

  return keysets->size() != 0;
}

bool KeysetManagement::GetVaultKeysetLabels(
    const std::string& obfuscated_username,
    std::vector<std::string>* labels) const {
  CHECK(labels);
  base::FilePath user_dir = ShadowRoot().Append(obfuscated_username);

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false /* Not recursive. */, base::FileEnumerator::FILES));
  base::FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    base::FilePath file_name = next_path.BaseName();
    // Scan for "master." files.
    if (file_name.RemoveFinalExtension().value() != kKeyFile) {
      continue;
    }
    int index = 0;
    std::string index_str = file_name.FinalExtension();
    // StringToInt will only return true for a perfect conversion.
    if (!base::StringToInt(&index_str[1], &index)) {
      continue;
    }
    if (index < 0 || index >= kKeyFileMax) {
      LOG(ERROR) << "Invalid key file range: " << index;
      continue;
    }
    // Now parse the keyset to get its label or skip it.
    std::unique_ptr<VaultKeyset> vk =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk) {
      continue;
    }
    labels->push_back(vk->label());
  }

  return (labels->size() > 0);
}

bool KeysetManagement::AddInitialKeyset(const Credentials& credentials) {
  const brillo::SecureBlob passkey = credentials.passkey();
  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> vk(
      vault_keyset_factory_->New(platform_, crypto_));
  vk->Initialize(platform_, crypto_);
  vk->CreateRandom();
  vk->set_legacy_index(kInitialKeysetIndex);

  if (credentials.key_data().type() == KeyData::KEY_TYPE_CHALLENGE_RESPONSE) {
    vk->mutable_serialized()->set_flags(
        vk->serialized().flags() |
        SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
    *vk->mutable_serialized()->mutable_signature_challenge_info() =
        credentials.challenge_credentials_keyset_info();
  }
  // Merge in the key data from credentials using the label() as
  // the existence test. (All new-format calls must populate the
  // label on creation.)
  if (!credentials.key_data().label().empty()) {
    *vk->mutable_serialized()->mutable_key_data() = credentials.key_data();
  }

  if (!vk->Encrypt(passkey, obfuscated_username) ||
      !vk->Save(VaultKeysetPath(obfuscated_username, kInitialKeysetIndex))) {
    LOG(ERROR) << "Failed to encrypt and write keyset for the new user.";
    return false;
  }

  return true;
}

bool KeysetManagement::ShouldReSaveKeyset(VaultKeyset* vault_keyset) const {
  // If the vault keyset's TPM state is not the same as that configured for
  // the device, re-save the keyset (this will save in the device's default
  // method).
  // In the table below: X = true, - = false, * = any value
  //
  //                 1   2   3   4   5   6   7   8   9
  // should_tpm      X   X   X   X   -   -   -   *   X
  //
  // pcr_bound       -   X   *   -   -   *   -   *   -
  //
  // tpm_wrapped     -   X   X   -   -   X   -   X   *
  //
  // scrypt_wrapped  -   -   -   X   -   -   X   X   *
  //
  // scrypt_derived  *   X   -   *   *   *   *   *   *
  //
  // migrate         Y   N   Y   Y   Y   Y   N   Y   Y
  //
  // If the vault keyset is signature-challenge protected, we should not
  // re-encrypt it at all (that is unnecessary).
  const unsigned crypt_flags = vault_keyset->serialized().flags();
  bool pcr_bound = (crypt_flags & SerializedVaultKeyset::PCR_BOUND) != 0;
  bool tpm_wrapped = (crypt_flags & SerializedVaultKeyset::TPM_WRAPPED) != 0;
  bool scrypt_wrapped =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_WRAPPED) != 0;
  bool scrypt_derived =
      (crypt_flags & SerializedVaultKeyset::SCRYPT_DERIVED) != 0;
  bool is_signature_challenge_protected =
      (crypt_flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) != 0;
  bool should_tpm = (crypto_->is_cryptohome_key_loaded() &&
                     !is_signature_challenge_protected);
  bool can_unseal_with_user_auth = crypto_->CanUnsealWithUserAuth();
  bool has_tpm_public_key_hash =
      vault_keyset->serialized().has_tpm_public_key_hash();

  if (is_signature_challenge_protected) {
    return false;
  }

  bool is_le_credential =
      (crypt_flags & SerializedVaultKeyset::LE_CREDENTIAL) != 0;
  uint64_t le_label = vault_keyset->serialized().le_label();
  if (is_le_credential && !crypto_->NeedsPcrBinding(le_label)) {
    return false;
  }

  // If the keyset was TPM-wrapped, but there was no public key hash,
  // always re-save.
  if (tpm_wrapped && !has_tpm_public_key_hash) {
    LOG(INFO) << "Migrating keyset " << vault_keyset->legacy_index()
              << " as there is no public hash";
    return true;
  }

  // Check the table.
  if (tpm_wrapped && should_tpm && scrypt_derived && !scrypt_wrapped) {
    if ((pcr_bound && can_unseal_with_user_auth) ||
        (!pcr_bound && !can_unseal_with_user_auth)) {
      return false;  // 2
    }
  }
  if (scrypt_wrapped && !should_tpm && !tpm_wrapped)
    return false;  // 7

  LOG(INFO) << "Migrating keyset " << vault_keyset->legacy_index()
            << ": should_tpm=" << should_tpm
            << ", has_hash=" << has_tpm_public_key_hash
            << ", flags=" << crypt_flags << ", pcr_bound=" << pcr_bound
            << ", can_unseal_with_user_auth=" << can_unseal_with_user_auth;

  return true;
}

bool KeysetManagement::ReSaveKeyset(const Credentials& credentials,
                                    VaultKeyset* keyset) const {
  // Save the initial serialized proto so we can roll-back any changes if we
  // failed to re-save.
  SerializedVaultKeyset old_serialized;
  old_serialized.CopyFrom(keyset->serialized());

  std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);

  uint64_t label = keyset->serialized().le_label();
  if (!keyset->Encrypt(credentials.passkey(), obfuscated_username) ||
      !keyset->Save(keyset->source_file())) {
    LOG(ERROR) << "Failed to encrypt and write the keyset.";
    keyset->mutable_serialized()->CopyFrom(old_serialized);
    return false;
  }

  if ((keyset->serialized().flags() & SerializedVaultKeyset::LE_CREDENTIAL) !=
      0) {
    if (!crypto_->RemoveLECredential(label)) {
      // This is non-fatal error.
      LOG(ERROR) << "Failed to remove label = " << label;
    }
  }

  return true;
}

bool KeysetManagement::ReSaveKeysetIfNeeded(const Credentials& credentials,
                                            VaultKeyset* keyset) const {
  // Calling EnsureTpm here handles the case where a user logged in while
  // cryptohome was taking TPM ownership.  In that case, their vault keyset
  // would be scrypt-wrapped and the TPM would not be connected.  If we're
  // configured to use the TPM, calling EnsureTpm will try to connect, and
  // if successful, the call to has_tpm() below will succeed, allowing
  // re-wrapping (migration) using the TPM.
  crypto_->EnsureTpm(false);

  bool force_resave = false;
  if (!keyset->serialized().has_wrapped_chaps_key()) {
    keyset->CreateRandomChapsKey();
    force_resave = true;
  }

  if (force_resave || ShouldReSaveKeyset(keyset)) {
    return ReSaveKeyset(credentials, keyset);
  }

  return true;
}

std::unique_ptr<VaultKeyset> KeysetManagement::LoadUnwrappedKeyset(
    const Credentials& credentials, MountError* error) {
  if (error) {
    *error = MOUNT_ERROR_NONE;
  }

  std::unique_ptr<VaultKeyset> vk = GetValidKeyset(credentials, error);

  if (!vk) {
    LOG(INFO) << "Could not find keyset matching credentials for user: "
              << credentials.username();
    return nullptr;
  }

  // TODO(dlunev): we shall start checking whether re-save succeeded. We are not
  // adding the check during the refactor to preserve behaviour.
  ReSaveKeysetIfNeeded(credentials, vk.get());

  return vk;
}

CryptohomeErrorCode KeysetManagement::AddKeyset(
    const Credentials& existing_credentials,
    const brillo::SecureBlob& new_passkey,
    const KeyData* new_data,  // NULLable
    bool clobber,
    int* index) {
  // TODO(wad) Determine how to best bubble up the failures MOUNT_ERROR
  //           encapsulate wrt the TPM behavior.
  std::string obfuscated =
      existing_credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> vk =
      GetValidKeyset(existing_credentials, nullptr /* error */);
  if (!vk) {
    // Differentiate between failure and non-existent.
    if (!existing_credentials.key_data().label().empty()) {
      vk = GetVaultKeyset(obfuscated, existing_credentials.key_data().label());
      if (!vk.get()) {
        LOG(WARNING) << "AddKeyset: key not found";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "AddKeyset: invalid authentication provided";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // Check the privileges to ensure Add is allowed.
  // Keys without extended data are considered fully privileged.
  if (vk->serialized().has_key_data() &&
      !vk->serialized().key_data().privileges().add()) {
    // TODO(wad) Ensure this error can be returned as a KEY_DENIED error
    //           for AddKeyEx.
    LOG(WARNING) << "AddKeyset: no add() privilege";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED;
  }

  // If the VaultKeyset doesn't have a reset seed, simply generate
  // one and re-encrypt before proceeding.
  if (!vk->serialized().has_wrapped_reset_seed()) {
    LOG(INFO) << "Keyset lacks reset_seed; generating one.";
    vk->CreateRandomResetSeed();
    if (!vk->Encrypt(existing_credentials.passkey(), obfuscated) ||
        !vk->Save(vk->source_file())) {
      LOG(WARNING) << "Failed to re-encrypt the old keyset";
      return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
    }
  }

  // Walk the namespace looking for the first free spot.
  // Optimizations can come later.
  // Note, nothing is stopping simultaenous access to these files
  // or enforcing mandatory locking.
  int new_index = 0;
  FILE* vk_file = NULL;
  base::FilePath vk_path;
  for (; new_index < kKeyFileMax; ++new_index) {
    vk_path = VaultKeysetPath(obfuscated, new_index);
    // Rely on fopen()'s O_EXCL|O_CREAT behavior to fail
    // repeatedly until there is an opening.
    // TODO(wad) Add a clean-up-0-byte-keysets helper to c-home startup
    vk_file = platform_->OpenFile(vk_path, "wx");
    if (vk_file)  // got one
      break;
  }

  if (!vk_file) {
    LOG(WARNING) << "Failed to find an available keyset slot";
    return CRYPTOHOME_ERROR_KEY_QUOTA_EXCEEDED;
  }
  // Once the file has been claimed, we can release the handle.
  platform_->CloseFile(vk_file);

  // Before persisting, check, in a racy-way, if there is
  // an existing labeled credential.
  if (new_data) {
    std::unique_ptr<VaultKeyset> match =
        GetVaultKeyset(obfuscated, new_data->label());
    if (match.get()) {
      LOG(INFO) << "Label already exists.";
      platform_->DeleteFile(vk_path);
      if (!clobber) {
        return CRYPTOHOME_ERROR_KEY_LABEL_EXISTS;
      }
      new_index = match->legacy_index();
      vk_path = match->source_file();
    }
  }
  // Since we're reusing the authorizing VaultKeyset, be careful with the
  // metadata.
  vk->mutable_serialized()->clear_key_data();
  if (new_data) {
    *(vk->mutable_serialized()->mutable_key_data()) = *new_data;
  }

  // Repersist the VK with the new creds.
  CryptohomeErrorCode added = CRYPTOHOME_ERROR_NOT_SET;
  if (!vk->Encrypt(new_passkey, obfuscated) || !vk->Save(vk_path)) {
    LOG(WARNING) << "Failed to encrypt or write the new keyset";
    added = CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
    // If we're clobbering, don't delete on error.
    if (!clobber) {
      platform_->DeleteFile(vk_path);
    }
  } else {
    *index = new_index;
  }
  return added;
}

CryptohomeErrorCode KeysetManagement::RemoveKeyset(
    const Credentials& credentials, const KeyData& key_data) {
  // This error condition should be caught by the caller.
  if (key_data.label().empty())
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;

  const std::string obfuscated =
      credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> remove_vk =
      GetVaultKeyset(obfuscated, key_data.label());
  if (!remove_vk.get()) {
    LOG(WARNING) << "RemoveKeyset: key to remove not found";
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  std::unique_ptr<VaultKeyset> vk =
      GetValidKeyset(credentials, nullptr /* error */);
  if (!vk) {
    // Differentiate between failure and non-existent.
    if (!credentials.key_data().label().empty()) {
      vk = GetVaultKeyset(obfuscated, credentials.key_data().label());
      if (!vk.get()) {
        LOG(WARNING) << "RemoveKeyset: key not found";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "RemoveKeyset: invalid authentication provided";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  // Legacy keys can remove any other key. Otherwise a key needs explicit
  // privileges.
  if (vk->serialized().has_key_data() &&
      !vk->serialized().key_data().privileges().remove()) {
    LOG(WARNING) << "RemoveKeyset: no remove() privilege";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED;
  }

  if (!ForceRemoveKeyset(obfuscated, remove_vk->legacy_index())) {
    LOG(ERROR) << "RemoveKeyset: failed to remove keyset file";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }
  return CRYPTOHOME_ERROR_NOT_SET;
}

bool KeysetManagement::ForceRemoveKeyset(const std::string& obfuscated,
                                         int index) {
  // Note, external callers should check credentials.
  if (index < 0 || index >= kKeyFileMax)
    return false;

  std::unique_ptr<VaultKeyset> vk = LoadVaultKeysetForUser(obfuscated, index);
  if (!vk) {
    LOG(WARNING) << "ForceRemoveKeyset: keyset " << index << " for "
                 << obfuscated << " does not exist";
    // Since it doesn't exist, then we're done.
    return true;
  }

  // Try removing the LE credential data, if applicable. But, don't abort if we
  // fail. The leaf data will remain, but at least the SerializedVaultKeyset
  // will be deleted.
  if (vk->IsLECredential()) {
    if (!crypto_->RemoveLECredential(vk->serialized().le_label())) {
      // TODO(crbug.com/809749): Add UMA logging for this failure.
      LOG(ERROR)
          << "ForceRemoveKeyset: Failed to remove LE credential metadata.";
    }
  }

  base::FilePath path = VaultKeysetPath(obfuscated, index);
  if (platform_->DeleteFileSecurely(path))
    return true;

  // TODO(wad) Add file zeroing here or centralize with other code.
  return platform_->DeleteFile(path);
}

bool KeysetManagement::MoveKeyset(const std::string& obfuscated,
                                  int src,
                                  int dst) {
  if (src < 0 || dst < 0 || src >= kKeyFileMax || dst >= kKeyFileMax)
    return false;

  base::FilePath src_path = VaultKeysetPath(obfuscated, src);
  base::FilePath dst_path = VaultKeysetPath(obfuscated, dst);
  if (!platform_->FileExists(src_path))
    return false;
  if (platform_->FileExists(dst_path))
    return false;
  // Grab the destination exclusively
  FILE* vk_file = platform_->OpenFile(dst_path, "wx");
  if (!vk_file)
    return false;
  // The creation occurred so there's no reason to keep the handle.
  platform_->CloseFile(vk_file);
  if (!platform_->Rename(src_path, dst_path))
    return false;
  return true;
}

std::unique_ptr<VaultKeyset> KeysetManagement::LoadVaultKeysetForUser(
    const std::string& obfuscated_user, int index) const {
  std::unique_ptr<VaultKeyset> keyset(
      vault_keyset_factory_->New(platform_, crypto_));
  // Load the encrypted keyset
  base::FilePath user_key_file = VaultKeysetPath(obfuscated_user, index);
  // We don't have keys yet, so just load it.
  // TODO(wad) Move to passing around keysets and not serialized versions.
  if (!keyset->Load(user_key_file)) {
    LOG(ERROR) << "Failed to load keyset file for user " << obfuscated_user;
    return nullptr;
  }
  keyset->set_legacy_index(index);
  return keyset;
}

bool KeysetManagement::Migrate(const Credentials& newcreds,
                               const brillo::SecureBlob& oldkey,
                               int* migrated_key_index) {
  CHECK(migrated_key_index);
  Credentials oldcreds(newcreds.username(), oldkey);
  std::string obfuscated = newcreds.GetObfuscatedUsername(system_salt_);

  int key_index = -1;
  std::unique_ptr<VaultKeyset> vk =
      GetValidKeyset(oldcreds, nullptr /* error */);
  if (!vk) {
    LOG(ERROR) << "Can not retrieve keyset for the user: "
               << newcreds.username();
    return false;
  }
  key_index = vk->legacy_index();
  if (key_index == -1) {
    LOG(ERROR) << "Attempted migration of key-less mount.";
    return false;
  }

  const KeyData* key_data = NULL;
  if (vk->serialized().has_key_data()) {
    key_data = &(vk->serialized().key_data());
    // legacy keys are full privs
    if (!vk->serialized().key_data().privileges().add() ||
        !vk->serialized().key_data().privileges().remove()) {
      LOG(ERROR) << "Migrate: key lacks sufficient privileges()";
      return false;
    }
  }

  int new_key_index = -1;
  // For a labeled key with the same label as the old key,
  //  this will overwrite the existing keyset file.
  if (AddKeyset(oldcreds, newcreds.passkey(), key_data, true, &new_key_index) !=
      CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Migrate: failed to add the new keyset";
    return false;
  }

  // For existing unlabeled keys, we need to remove the old key and swap
  // the slot.  If the key was labeled and clobbered, the key indices will
  // match.
  if (new_key_index != key_index) {
    if (!ForceRemoveKeyset(obfuscated, key_index)) {
      LOG(ERROR) << "Migrate: unable to delete the old keyset: " << key_index;
      // TODO(wad) Should we zero it or move it into space?
      // Fallthrough
    }
    // Put the new one in its slot.
    if (!MoveKeyset(obfuscated, new_key_index, key_index)) {
      // This is bad, but non-terminal since we have a valid, migrated key.
      LOG(ERROR) << "Migrate: failed to move the new key to the old slot";
      key_index = new_key_index;
    }
  }

  // Remove all other keysets during a "migration".
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "Failed to enumerate keysets after adding one. Weird.";
    // Fallthrough: The user is migrated, but something else changed keys.
  }
  for (int index : key_indices) {
    if (index == key_index)
      continue;
    LOG(INFO) << "Removing keyset " << index << " due to migration.";
    ForceRemoveKeyset(obfuscated, index);  // Failure is ok.
  }

  *migrated_key_index = key_index;

  return true;
}

void KeysetManagement::ResetLECredentials(const Credentials& creds) {
  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return;
  }

  bool credentials_checked = false;
  std::unique_ptr<VaultKeyset> vk;
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk_reset =
        LoadVaultKeysetForUser(obfuscated, index);
    if (!vk_reset || !vk_reset->IsLECredential() ||  // Skip non-LE Credentials.
        crypto_->GetWrongAuthAttempts(vk_reset->serialized()) == 0) {
      continue;
    }

    if (!credentials_checked) {
      // Make sure the credential can actually be used for sign-in.
      // It is also the easiest way to get a valid keyset.
      vk = GetValidKeyset(creds, nullptr /* error */);
      if (!vk) {
        LOG(WARNING) << "The provided credentials are incorrect or invalid"
                        " for LE credential reset, reset skipped.";
        return;
      }
      credentials_checked = true;
    }

    CryptoError err;
    if (!crypto_->ResetLECredential(vk_reset->serialized(), &err, *vk)) {
      LOG(WARNING) << "Failed to reset an LE credential: " << err;
    } else {
      vk_reset->mutable_serialized()
          ->mutable_key_data()
          ->mutable_policy()
          ->set_auth_locked(false);
      if (!vk_reset->Save(vk_reset->source_file())) {
        LOG(WARNING) << "Failed to clear auth_locked in VaultKeyset on disk.";
      }
    }
  }
}

void KeysetManagement::RemoveLECredentials(
    const std::string& obfuscated_username) {
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated_username;
    return;
  }

  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> vk_remove =
        LoadVaultKeysetForUser(obfuscated_username, index);
    if (!vk_remove ||
        !vk_remove->IsLECredential()) {  // Skip non-LE Credentials.
      continue;
    }

    uint64_t label = vk_remove->serialized().le_label();
    if (!crypto_->RemoveLECredential(label)) {
      LOG(WARNING) << "Failed to remove an LE credential, label: " << label;
      continue;
    }

    // Remove the cryptohome VaultKeyset data.
    base::FilePath vk_path = VaultKeysetPath(obfuscated_username, index);
    platform_->DeleteFile(vk_path);
  }
}

}  // namespace cryptohome
