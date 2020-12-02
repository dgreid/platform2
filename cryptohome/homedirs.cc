// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/homedirs.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/timer/elapsed_timer.h>
#include <brillo/cryptohome.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/key.pb.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/platform.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

namespace {
constexpr int kInitialKeysetIndex = 0;
constexpr char kTsFile[] = "timestamp";
}  // namespace

const char* kEmptyOwner = "";
// Each xattr is set to Android app internal data directory, contains
// 8-byte inode number of cache subdirectory.  See
// frameworks/base/core/java/android/app/ContextImpl.java
const char kAndroidCacheInodeAttribute[] = "user.inode_cache";
const char kAndroidCodeCacheInodeAttribute[] = "user.inode_code_cache";
const char kTrackedDirectoryNameAttribute[] = "user.TrackedDirectoryName";
const char kRemovableFileAttribute[] = "user.GCacheRemovable";
// Name of the vault directory which is used with eCryptfs cryptohome.
const char kEcryptfsVaultDir[] = "vault";
// Name of the mount directory.
const char kMountDir[] = "mount";

HomeDirs::HomeDirs(Platform* platform,
                   Crypto* crypto,
                   const base::FilePath& shadow_root,
                   const brillo::SecureBlob& system_salt,
                   UserOldestActivityTimestampCache* timestamp_cache,
                   std::unique_ptr<policy::PolicyProvider> policy_provider,
                   std::unique_ptr<VaultKeysetFactory> vault_keyset_factory)
    : platform_(platform),
      crypto_(crypto),
      shadow_root_(shadow_root),
      system_salt_(system_salt),
      timestamp_cache_(timestamp_cache),
      policy_provider_(std::move(policy_provider)),
      vault_keyset_factory_(std::move(vault_keyset_factory)),
      enterprise_owned_(false) {}

HomeDirs::~HomeDirs() {}

// static
FilePath HomeDirs::GetEcryptfsUserVaultPath(
    const FilePath& shadow_root, const std::string& obfuscated_username) {
  return shadow_root.Append(obfuscated_username).Append(kEcryptfsVaultDir);
}

// static
FilePath HomeDirs::GetUserMountDirectory(
    const FilePath& shadow_root, const std::string& obfuscated_username) {
  return shadow_root.Append(obfuscated_username).Append(kMountDir);
}

void HomeDirs::LoadDevicePolicy() {
  policy_provider_->Reload();
}

bool HomeDirs::AreEphemeralUsersEnabled() {
  LoadDevicePolicy();
  // If the policy cannot be loaded, default to non-ephemeral users.
  bool ephemeral_users_enabled = false;
  if (policy_provider_->device_policy_is_loaded())
    policy_provider_->GetDevicePolicy().GetEphemeralUsersEnabled(
        &ephemeral_users_enabled);
  return ephemeral_users_enabled;
}

bool HomeDirs::AreCredentialsValid(const Credentials& creds) {
  std::unique_ptr<VaultKeyset> vk = GetValidKeyset(creds, nullptr /* error */);
  return vk.get() != nullptr;
}

std::unique_ptr<VaultKeyset> HomeDirs::GetValidKeyset(const Credentials& creds,
                                                      MountError* error) {
  if (error)
    *error = MOUNT_ERROR_NONE;

  std::string owner;
  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);
  // |AreEphemeralUsers| will reload the policy to guarantee freshness.
  if (AreEphemeralUsersEnabled() && GetOwner(&owner) && obfuscated != owner) {
    if (error)
      *error = MOUNT_ERROR_FATAL;
    return nullptr;
  }

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

bool HomeDirs::SetLockedToSingleUser() const {
  return platform_->TouchFileDurable(base::FilePath(kLockedToSingleUserFile));
}

bool HomeDirs::Exists(const std::string& obfuscated_username) const {
  FilePath user_dir = shadow_root_.Append(obfuscated_username);
  return platform_->DirectoryExists(user_dir);
}

bool HomeDirs::CryptohomeExists(const std::string& obfuscated_username) const {
  return EcryptfsCryptohomeExists(obfuscated_username) ||
         DircryptoCryptohomeExists(obfuscated_username);
}

bool HomeDirs::EcryptfsCryptohomeExists(
    const std::string& obfuscated_username) const {
  // Check for the presence of a vault directory for ecryptfs.
  return platform_->DirectoryExists(
      GetEcryptfsUserVaultPath(obfuscated_username));
}

bool HomeDirs::DircryptoCryptohomeExists(
    const std::string& obfuscated_username) const {
  // Check for the presence of an encrypted mount directory for dircrypto.
  FilePath mount_path = GetUserMountDirectory(obfuscated_username);
  return platform_->DirectoryExists(mount_path) &&
         platform_->GetDirCryptoKeyState(mount_path) ==
             dircrypto::KeyState::ENCRYPTED;
}

FilePath HomeDirs::GetEcryptfsUserVaultPath(
    const std::string& obfuscated_username) const {
  return GetEcryptfsUserVaultPath(shadow_root_, obfuscated_username);
}

FilePath HomeDirs::GetUserMountDirectory(
    const std::string& obfuscated_username) const {
  return GetUserMountDirectory(shadow_root_, obfuscated_username);
}

std::unique_ptr<VaultKeyset> HomeDirs::GetVaultKeyset(
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
bool HomeDirs::GetVaultKeysets(const std::string& obfuscated,
                               std::vector<int>* keysets) const {
  CHECK(keysets);
  FilePath user_dir = shadow_root_.Append(obfuscated);

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false, base::FileEnumerator::FILES));
  FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    FilePath file_name = next_path.BaseName();
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

bool HomeDirs::GetVaultKeysetLabels(const std::string& obfuscated_username,
                                    std::vector<std::string>* labels) const {
  CHECK(labels);
  FilePath user_dir = shadow_root_.Append(obfuscated_username);

  std::unique_ptr<FileEnumerator> file_enumerator(platform_->GetFileEnumerator(
      user_dir, false /* Not recursive. */, base::FileEnumerator::FILES));
  FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    FilePath file_name = next_path.BaseName();
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

bool HomeDirs::AddInitialKeyset(const Credentials& credentials) {
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
      !vk->Save(GetVaultKeysetPath(obfuscated_username, kInitialKeysetIndex))) {
    LOG(ERROR) << "Failed to encrypt and write keyset for the new user.";
    return false;
  }
  UpdateActivityTimestamp(obfuscated_username, kInitialKeysetIndex, 0);

  return true;
}

bool HomeDirs::ShouldReSaveKeyset(VaultKeyset* vault_keyset) const {
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

bool HomeDirs::ReSaveKeyset(const Credentials& credentials,
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

bool HomeDirs::ReSaveKeysetIfNeeded(const Credentials& credentials,
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

std::unique_ptr<VaultKeyset> HomeDirs::LoadUnwrappedKeyset(
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

bool HomeDirs::UpdateActivityTimestamp(const std::string& obfuscated,
                                       int index,
                                       int time_shift_sec) {
  base::Time timestamp = platform_->GetCurrentTime();
  if (time_shift_sec > 0) {
    timestamp -= base::TimeDelta::FromSeconds(time_shift_sec);
  }

  Timestamp ts_proto;
  ts_proto.set_timestamp(timestamp.ToInternalValue());
  std::string timestamp_str;
  if (!ts_proto.SerializeToString(&timestamp_str)) {
    return false;
  }

  base::FilePath ts_file = GetUserActivityTimestampPath(obfuscated, index);
  if (!platform_->WriteStringToFileAtomicDurable(ts_file, timestamp_str,
                                                 kKeyFilePermissions)) {
    LOG(ERROR) << "Failed writing to timestamp file: " << ts_file;
    return false;
  }

  if (timestamp_cache_ && timestamp_cache_->initialized()) {
    timestamp_cache_->UpdateExistingUser(obfuscated, timestamp);
  }

  return true;
}

CryptohomeErrorCode HomeDirs::AddKeyset(const Credentials& existing_credentials,
                                        const SecureBlob& new_passkey,
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
  FilePath vk_path;
  for (; new_index < kKeyFileMax; ++new_index) {
    vk_path = GetVaultKeysetPath(obfuscated, new_index);
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
      platform_->DeleteFile(vk_path, false);
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
      platform_->DeleteFile(vk_path, false);
    }
  } else {
    *index = new_index;
  }
  return added;
}

CryptohomeErrorCode HomeDirs::RemoveKeyset(const Credentials& credentials,
                                           const KeyData& key_data) {
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

bool HomeDirs::ForceRemoveKeyset(const std::string& obfuscated, int index) {
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

  FilePath path = GetVaultKeysetPath(obfuscated, index);
  if (platform_->DeleteFileSecurely(path))
    return true;

  // TODO(wad) Add file zeroing here or centralize with other code.
  return platform_->DeleteFile(path, false);
}

bool HomeDirs::MoveKeyset(const std::string& obfuscated, int src, int dst) {
  if (src < 0 || dst < 0 || src >= kKeyFileMax || dst >= kKeyFileMax)
    return false;

  FilePath src_path = GetVaultKeysetPath(obfuscated, src);
  FilePath dst_path = GetVaultKeysetPath(obfuscated, dst);
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

FilePath HomeDirs::GetVaultKeysetPath(const std::string& obfuscated,
                                      int index) const {
  return shadow_root_.Append(obfuscated)
      .Append(kKeyFile)
      .AddExtension(base::NumberToString(index));
}

FilePath HomeDirs::GetUserActivityTimestampPath(const std::string& obfuscated,
                                                int index) const {
  return GetVaultKeysetPath(obfuscated, index).AddExtension(kTsFile);
}

void HomeDirs::RemoveNonOwnerCryptohomesCallback(
    const std::string& obfuscated) {
  if (!enterprise_owned_) {  // Enterprise owned? Delete it all.
    std::string owner;
    if (!GetOwner(&owner) || obfuscated == owner)
      return;
  }
  // Once we're sure this is not the owner's cryptohome, delete it.
  RemoveLECredentials(obfuscated);
  FilePath shadow_dir = shadow_root_.Append(obfuscated);
  platform_->DeleteFile(shadow_dir, true);
}

void HomeDirs::RemoveNonOwnerCryptohomes() {
  std::string owner;
  if (!enterprise_owned_ && !GetOwner(&owner))
    return;

  auto homedirs = GetHomeDirs();
  FilterMountedHomedirs(&homedirs);

  RemoveNonOwnerCryptohomesInternal(homedirs);
}

void HomeDirs::RemoveNonOwnerCryptohomesInternal(
    const std::vector<HomeDir>& homedirs) {
  std::string owner;
  if (!enterprise_owned_ && !GetOwner(&owner))
    return;

  for (const auto& dir : homedirs) {
    HomeDirs::RemoveNonOwnerCryptohomesCallback(dir.obfuscated);
  }

  // TODO(ellyjones): is this valuable? These two directories should just be
  // mountpoints.
  RemoveNonOwnerDirectories(brillo::cryptohome::home::GetUserPathPrefix());
  RemoveNonOwnerDirectories(brillo::cryptohome::home::GetRootPathPrefix());
}

std::vector<HomeDirs::HomeDir> HomeDirs::GetHomeDirs() {
  std::vector<HomeDirs::HomeDir> ret;
  std::vector<FilePath> entries;
  if (!platform_->EnumerateDirectoryEntries(shadow_root_, false, &entries)) {
    return ret;
  }

  for (const auto& entry : entries) {
    HomeDirs::HomeDir dir;

    dir.obfuscated = entry.BaseName().value();

    if (!brillo::cryptohome::home::IsSanitizedUserName(dir.obfuscated))
      continue;

    if (!platform_->DirectoryExists(
            brillo::cryptohome::home::GetHashedUserPath(dir.obfuscated)))
      continue;

    ret.push_back(dir);
  }

  std::vector<FilePath> user_paths;
  std::transform(
      ret.begin(), ret.end(), std::back_inserter(user_paths),
      [](const HomeDirs::HomeDir& homedir) {
        return brillo::cryptohome::home::GetHashedUserPath(homedir.obfuscated);
      });

  auto is_mounted = platform_->AreDirectoriesMounted(user_paths);

  if (!is_mounted)
    return ret;  // assume all are unmounted

  int i = 0;
  for (const bool& m : is_mounted.value()) {
    ret[i++].is_mounted = m;
  }

  return ret;
}

void HomeDirs::FilterMountedHomedirs(std::vector<HomeDirs::HomeDir>* homedirs) {
  homedirs->erase(std::remove_if(homedirs->begin(), homedirs->end(),
                                 [](const HomeDirs::HomeDir& dir) {
                                   return dir.is_mounted;
                                 }),
                  homedirs->end());
}

void HomeDirs::RemoveNonOwnerDirectories(const FilePath& prefix) {
  std::vector<FilePath> dirents;
  if (!platform_->EnumerateDirectoryEntries(prefix, false, &dirents))
    return;
  std::string owner;
  if (!enterprise_owned_ && !GetOwner(&owner))
    return;
  for (const auto& dirent : dirents) {
    const std::string basename = dirent.BaseName().value();
    if (!enterprise_owned_ && !strcasecmp(basename.c_str(), owner.c_str()))
      continue;  // Skip the owner's directory.
    if (!brillo::cryptohome::home::IsSanitizedUserName(basename))
      continue;  // Skip any directory whose name is not an obfuscated user
                 // name.
    if (platform_->IsDirectoryMounted(dirent))
      continue;  // Skip any directory that is currently mounted.
    platform_->DeleteFile(dirent, true);
  }
}

bool HomeDirs::GetTrackedDirectory(const FilePath& user_dir,
                                   const FilePath& tracked_dir_name,
                                   FilePath* out) {
  FilePath vault_path = user_dir.Append(kEcryptfsVaultDir);
  if (platform_->DirectoryExists(vault_path)) {
    // On Ecryptfs, tracked directories' names are not encrypted.
    *out = user_dir.Append(kEcryptfsVaultDir).Append(tracked_dir_name);
    return true;
  }
  // This is dircrypto. Use the xattr to locate the directory.
  return GetTrackedDirectoryForDirCrypto(user_dir.Append(kMountDir),
                                         tracked_dir_name, out);
}

bool HomeDirs::GetTrackedDirectoryForDirCrypto(const FilePath& mount_dir,
                                               const FilePath& tracked_dir_name,
                                               FilePath* out) {
  FilePath current_name;
  FilePath current_path = mount_dir;

  // Iterate over name components. This way, we don't have to inspect every
  // directory under |mount_dir|.
  std::vector<std::string> name_components;
  tracked_dir_name.GetComponents(&name_components);
  for (const auto& name_component : name_components) {
    FilePath next_path;
    std::unique_ptr<FileEnumerator> enumerator(
        platform_->GetFileEnumerator(current_path, false /* recursive */,
                                     base::FileEnumerator::DIRECTORIES));
    for (FilePath dir = enumerator->Next(); !dir.empty();
         dir = enumerator->Next()) {
      if (platform_->HasExtendedFileAttribute(dir,
                                              kTrackedDirectoryNameAttribute)) {
        std::string name;
        if (!platform_->GetExtendedFileAttributeAsString(
                dir, kTrackedDirectoryNameAttribute, &name))
          return false;
        if (name == name_component) {
          // This is the directory we're looking for.
          next_path = dir;
          break;
        }
      }
    }
    if (next_path.empty()) {
      LOG(ERROR) << "Tracked dir not found " << tracked_dir_name.value();
      return false;
    }
    current_path = next_path;
  }
  *out = current_path;
  return true;
}

void HomeDirs::AddUserTimestampToCache(const std::string& obfuscated) {
  //  Add a timestamp for every key.
  std::vector<int> key_indices;
  // Failure is okay since the loop falls through.
  GetVaultKeysets(obfuscated, &key_indices);
  // Collect the most recent time for a given user by walking all
  // vaults.  This avoids trying to keep them in sync atomically.
  // TODO(wad,?) Move non-key vault metadata to a standalone file.
  base::Time timestamp = base::Time();
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> keyset =
        LoadVaultKeysetForUser(obfuscated, index);
    if (keyset.get() && keyset->serialized().has_last_activity_timestamp()) {
      const base::Time t = base::Time::FromInternalValue(
          keyset->serialized().last_activity_timestamp());
      if (t > timestamp)
        timestamp = t;
    }
  }
  if (!timestamp.is_null()) {
    timestamp_cache_->AddExistingUser(obfuscated, timestamp);
  }
}

std::unique_ptr<VaultKeyset> HomeDirs::LoadVaultKeysetForUser(
    const std::string& obfuscated_user, int index) const {
  std::unique_ptr<VaultKeyset> keyset(
      vault_keyset_factory_->New(platform_, crypto_));
  // Load the encrypted keyset
  FilePath user_key_file = GetVaultKeysetPath(obfuscated_user, index);
  // We don't have keys yet, so just load it.
  // TODO(wad) Move to passing around keysets and not serialized versions.
  if (!keyset->Load(user_key_file)) {
    LOG(ERROR) << "Failed to load keyset file for user " << obfuscated_user;
    return nullptr;
  }
  keyset->set_legacy_index(index);
  return keyset;
}

bool HomeDirs::GetPlainOwner(std::string* owner) {
  LoadDevicePolicy();
  if (!policy_provider_->device_policy_is_loaded())
    return false;
  policy_provider_->GetDevicePolicy().GetOwner(owner);
  return true;
}

bool HomeDirs::GetOwner(std::string* owner) {
  std::string plain_owner;
  if (!GetPlainOwner(&plain_owner) || plain_owner.empty())
    return false;

  *owner = SanitizeUserNameWithSalt(plain_owner, system_salt_);
  return true;
}

bool HomeDirs::IsOrWillBeOwner(const std::string& account_id) {
  std::string owner;
  GetPlainOwner(&owner);
  return !enterprise_owned_ && (owner.empty() || account_id == owner);
}

bool HomeDirs::GetSystemSalt(SecureBlob* blob) {
  *blob = system_salt_;
  return true;
}

bool HomeDirs::Create(const std::string& username) {
  brillo::ScopedUmask scoped_umask(kDefaultUmask);
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);

  // Create the user's entry in the shadow root
  FilePath user_dir = shadow_root_.Append(obfuscated_username);
  if (!platform_->CreateDirectory(user_dir)) {
    return false;
  }

  return true;
}

bool HomeDirs::Remove(const std::string& username) {
  std::string obfuscated = SanitizeUserNameWithSalt(username, system_salt_);
  RemoveLECredentials(obfuscated);

  FilePath user_dir = shadow_root_.Append(obfuscated);
  FilePath user_path = brillo::cryptohome::home::GetUserPath(username);
  FilePath root_path = brillo::cryptohome::home::GetRootPath(username);
  return platform_->DeleteFile(user_dir, true) &&
         platform_->DeleteFile(user_path, true) &&
         platform_->DeleteFile(root_path, true);
}

bool HomeDirs::Rename(const std::string& account_id_from,
                      const std::string& account_id_to) {
  if (account_id_from == account_id_to) {
    return true;
  }

  const std::string obfuscated_from =
      SanitizeUserNameWithSalt(account_id_from, system_salt_);
  const std::string obfuscated_to =
      SanitizeUserNameWithSalt(account_id_to, system_salt_);

  const FilePath user_dir_from = shadow_root_.Append(obfuscated_from);
  const FilePath user_path_from =
      brillo::cryptohome::home::GetUserPath(account_id_from);
  const FilePath root_path_from =
      brillo::cryptohome::home::GetRootPath(account_id_from);
  const FilePath new_user_path_from =
      FilePath(MountHelper::GetNewUserPath(account_id_from));

  const FilePath user_dir_to = shadow_root_.Append(obfuscated_to);
  const FilePath user_path_to =
      brillo::cryptohome::home::GetUserPath(account_id_to);
  const FilePath root_path_to =
      brillo::cryptohome::home::GetRootPath(account_id_to);
  const FilePath new_user_path_to =
      FilePath(MountHelper::GetNewUserPath(account_id_to));

  LOG(INFO) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
            << account_id_to << "'):"
            << " renaming '" << user_dir_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_dir_from) << ") "
            << "=> '" << user_dir_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_dir_to) << "); "
            << "renaming '" << user_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_path_from) << ") "
            << "=> '" << user_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(user_path_to) << "); "
            << "renaming '" << root_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(root_path_from) << ") "
            << "=> '" << root_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(root_path_to) << "); "
            << "renaming '" << new_user_path_from.value() << "' "
            << "(exists=" << platform_->DirectoryExists(new_user_path_from)
            << ") "
            << "=> '" << new_user_path_to.value() << "' "
            << "(exists=" << platform_->DirectoryExists(new_user_path_to)
            << ")";

  const bool already_renamed = !platform_->DirectoryExists(user_dir_from);

  if (already_renamed) {
    LOG(INFO) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
              << account_id_to << "'): Consider already renamed. "
              << "('" << user_dir_from.value() << "' doesn't exist.)";
    return true;
  }

  const bool can_rename = !platform_->DirectoryExists(user_dir_to);

  if (!can_rename) {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): Destination already exists! "
               << " '" << user_dir_from.value() << "' "
               << "(exists=" << platform_->DirectoryExists(user_dir_from)
               << ") "
               << "=> '" << user_dir_to.value() << "' "
               << "(exists=" << platform_->DirectoryExists(user_dir_to)
               << "); ";
    return false;
  }

  // |user_dir_renamed| is return value, because three other directories are
  // empty and will be created as needed.
  const bool user_dir_renamed = !platform_->DirectoryExists(user_dir_from) ||
                                platform_->Rename(user_dir_from, user_dir_to);

  if (user_dir_renamed) {
    constexpr bool kIsRecursive = true;
    const bool user_path_deleted =
        platform_->DeleteFile(user_path_from, kIsRecursive);
    const bool root_path_deleted =
        platform_->DeleteFile(root_path_from, kIsRecursive);
    const bool new_user_path_deleted =
        platform_->DeleteFile(new_user_path_from, kIsRecursive);
    if (!user_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete user_path.";
    }
    if (!root_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete root_path.";
    }
    if (!new_user_path_deleted) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to delete new_user_path.";
    }
  } else {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): failed to rename user_dir.";
  }

  return user_dir_renamed;
}

int64_t HomeDirs::ComputeDiskUsage(const std::string& account_id) {
  // SanitizeUserNameWithSalt below doesn't accept empty username.
  if (account_id.empty()) {
    // Empty account is always non-existent, return 0 as specified.
    return 0;
  }

  // Note that for ephemeral mounts, there could be a vault that's not
  // ephemeral, but the current mount is ephemeral. In this case,
  // ComputeDiskUsage() return the non ephemeral on disk vault's size.
  std::string obfuscated = SanitizeUserNameWithSalt(account_id, system_salt_);
  FilePath user_dir = FilePath(shadow_root_).Append(obfuscated);

  int64_t size = 0;
  if (!platform_->DirectoryExists(user_dir)) {
    // It's either ephemeral or the user doesn't exist. In either case, we check
    // /home/user/$hash.
    FilePath user_home_dir = brillo::cryptohome::home::GetUserPath(account_id);
    size = platform_->ComputeDirectoryDiskUsage(user_home_dir);
  } else {
    // Note that we'll need to handle both ecryptfs and dircrypto.
    // dircrypto:
    // /home/.shadow/$hash/mount: Always equal to the size occupied.
    // ecryptfs:
    // /home/.shadow/$hash/vault: Always equal to the size occupied.
    // /home/.shadow/$hash/mount: Equal to the size occupied only when mounted.
    // Therefore, we check to see if vault exists, if it exists, we compute
    // vault's size, otherwise, we check mount's size.
    FilePath mount_dir = user_dir.Append(kMountDir);
    FilePath vault_dir = user_dir.Append(kEcryptfsVaultDir);
    if (platform_->DirectoryExists(vault_dir)) {
      // ecryptfs
      size = platform_->ComputeDirectoryDiskUsage(vault_dir);
    } else {
      // dircrypto
      size = platform_->ComputeDirectoryDiskUsage(mount_dir);
    }
  }
  if (size > 0) {
    return size;
  }
  return 0;
}

bool HomeDirs::Migrate(const Credentials& newcreds,
                       const SecureBlob& oldkey,
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

namespace {
const char* kChapsDaemonName = "chaps";
const char* kChapsDirName = ".chaps";
const char* kChapsSaltName = "auth_data_salt";
}  // namespace

FilePath HomeDirs::GetChapsTokenDir(const std::string& user) const {
  return brillo::cryptohome::home::GetDaemonStorePath(user, kChapsDaemonName);
}

FilePath HomeDirs::GetLegacyChapsTokenDir(const std::string& user) const {
  return brillo::cryptohome::home::GetUserPath(user).Append(kChapsDirName);
}

FilePath HomeDirs::GetChapsTokenSaltPath(const std::string& user) const {
  return GetChapsTokenDir(user).Append(kChapsSaltName);
}

bool HomeDirs::NeedsDircryptoMigration(
    const std::string& obfuscated_username) const {
  // Bail if dircrypto is not supported.
  const dircrypto::KeyState state =
      platform_->GetDirCryptoKeyState(shadow_root_);
  if (state == dircrypto::KeyState::UNKNOWN ||
      state == dircrypto::KeyState::NOT_SUPPORTED) {
    return false;
  }

  // Use the existence of eCryptfs vault as a single of whether the user needs
  // dircrypto migration. eCryptfs test is adapted from
  // Mount::DoesEcryptfsCryptohomeExist.
  const FilePath user_ecryptfs_vault_dir =
      shadow_root_.Append(obfuscated_username).Append(kEcryptfsVaultDir);
  return platform_->DirectoryExists(user_ecryptfs_vault_dir);
}

void HomeDirs::ResetLECredentials(const Credentials& creds) {
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

void HomeDirs::RemoveLECredentials(const std::string& obfuscated_username) {
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
    base::FilePath vk_path = GetVaultKeysetPath(obfuscated_username, index);
    platform_->DeleteFile(vk_path, true);
  }
}

int32_t HomeDirs::GetUnmountedAndroidDataCount() {
  const auto homedirs = GetHomeDirs();

  return std::count_if(
      homedirs.begin(), homedirs.end(), [&](const HomeDirs::HomeDir& dir) {
        if (dir.is_mounted)
          return false;

        if (EcryptfsCryptohomeExists(dir.obfuscated))
          return false;

        FilePath shadow_dir = shadow_root_.Append(dir.obfuscated);
        FilePath root_home_dir;
        return GetTrackedDirectory(shadow_dir, FilePath(kRootHomeSuffix),
                                   &root_home_dir) &&
               MayContainAndroidData(root_home_dir);
      });
}

bool HomeDirs::MayContainAndroidData(
    const base::FilePath& root_home_dir) const {
  // The root home directory is considered to contain Android data if its
  // grandchild (supposedly android-data/data) is owned by android's system UID.
  std::unique_ptr<FileEnumerator> dir_enum(platform_->GetFileEnumerator(
      root_home_dir, false, base::FileEnumerator::DIRECTORIES));
  for (base::FilePath subdirectory = dir_enum->Next(); !subdirectory.empty();
       subdirectory = dir_enum->Next()) {
    if (LooksLikeAndroidData(subdirectory)) {
      return true;
    }
  }
  return false;
}

bool HomeDirs::LooksLikeAndroidData(const base::FilePath& directory) const {
  std::unique_ptr<FileEnumerator> dir_enum(platform_->GetFileEnumerator(
      directory, false, base::FileEnumerator::DIRECTORIES));

  for (base::FilePath subdirectory = dir_enum->Next(); !subdirectory.empty();
       subdirectory = dir_enum->Next()) {
    if (IsOwnedByAndroidSystem(subdirectory)) {
      return true;
    }
  }
  return false;
}

bool HomeDirs::IsOwnedByAndroidSystem(const base::FilePath& directory) const {
  uid_t uid = 0;
  gid_t gid = 0;
  if (!platform_->GetOwnership(directory, &uid, &gid, false)) {
    return false;
  }
  return uid == kAndroidSystemUid + kArcContainerShiftUid;
}

}  // namespace cryptohome
