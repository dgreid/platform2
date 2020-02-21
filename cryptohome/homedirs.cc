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
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>

#include "cryptohome/credentials.h"
#include "cryptohome/crypto.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/mount.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/obfuscated_username.h"
#include "cryptohome/platform.h"
#include "cryptohome/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"

#include "key.pb.h"  // NOLINT(build/include)
#include "signed_secret.pb.h"  // NOLINT(build/include)

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

const char *kShadowRoot = "/home/.shadow";
const char *kEmptyOwner = "";
// xattr used for ARC++ M. No longer used since end of 2017, should be removed.
const char kAndroidCacheFilesAttribute[] = "user.AndroidCache";
// Each xattr is set to Android app internal data directory, contains
// 8-byte inode number of cache subdirectory.  Used in ARC++ N, see
// frameworks/base/core/java/android/app/ContextImpl.java
const char kAndroidCacheInodeAttribute[] = "user.inode_cache";
const char kAndroidCodeCacheInodeAttribute[] = "user.inode_code_cache";
const char kTrackedDirectoryNameAttribute[] = "user.TrackedDirectoryName";
const char kRemovableFileAttribute[] = "user.GCacheRemovable";
// Name of the vault directory which is used with eCryptfs cryptohome.
const char kEcryptfsVaultDir[] = "vault";
// Name of the mount directory.
const char kMountDir[] = "mount";

namespace {

// Returns label of the given serialized vault keyset. The label is normally
// specified in the keyset itself, but for a legacy keyset it has to be
// automatically generated.
std::string GetSerializedKeysetLabel(const SerializedVaultKeyset& serialized,
                                     int key_index) {
  if (!serialized.has_key_data()) {
    // Fallback for legacy keys, for which the label has to be inferred from the
    // index number.
    return base::StringPrintf("%s%d", kKeyLegacyPrefix, key_index);
  }
  return serialized.key_data().label();
}

void RemoveAllRemovableFiles(Platform* platform, const FilePath& dir) {
  std::unique_ptr<FileEnumerator> file_enumerator(
      platform->GetFileEnumerator(dir, true, base::FileEnumerator::FILES));
  for (FilePath file = file_enumerator->Next(); !file.empty();
       file = file_enumerator->Next()) {
    if (platform->HasNoDumpFileAttribute(file)
        || platform->HasExtendedFileAttribute(file, kRemovableFileAttribute) ) {
      if (!platform->DeleteFile(file, false)) {
        PLOG(WARNING) << "DeleteFile: " << file.value();
      }
    }
  }
}

}  // namespace

HomeDirs::HomeDirs()
    : default_platform_(new Platform()),
      platform_(default_platform_.get()),
      shadow_root_(FilePath(kShadowRoot)),
      timestamp_cache_(NULL),
      enterprise_owned_(false),
      default_policy_provider_(new policy::PolicyProvider()),
      policy_provider_(default_policy_provider_.get()),
      crypto_(NULL),
      default_mount_factory_(new MountFactory()),
      mount_factory_(default_mount_factory_.get()),
      default_vault_keyset_factory_(new VaultKeysetFactory()),
      vault_keyset_factory_(default_vault_keyset_factory_.get()) { }

HomeDirs::~HomeDirs() { }

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

bool HomeDirs::Init(Platform* platform, Crypto* crypto,
                    UserOldestActivityTimestampCache *cache) {
  platform_ = platform;
  crypto_ = crypto;
  timestamp_cache_ = cache;

  LoadDevicePolicy();
  if (!platform_->DirectoryExists(shadow_root_))
    platform_->CreateDirectory(shadow_root_);
  return GetSystemSalt(NULL);
}

bool HomeDirs::IsFreableDiskSpaceAvaible() {
  if (!enterprise_owned_)
    return false;

  const auto homedirs = GetHomeDirs();

  int unmounted_cryptohomes =
      std::count_if(homedirs.begin(), homedirs.end(),
                    [](auto& dir) { return !dir.is_mounted; });

  return unmounted_cryptohomes > 0;
}

void HomeDirs::FreeDiskSpace() {
  auto free_space = AmountOfFreeDiskSpace();

  switch (GetFreeDiskSpaceState(free_space)) {
    case HomeDirs::FreeSpaceState::kAboveTarget:
    case HomeDirs::FreeSpaceState::kAboveThreshold:
      // Already have enough space. No need to clean up.
      return;

    case HomeDirs::FreeSpaceState::kNeedNormalCleanup:
    case HomeDirs::FreeSpaceState::kNeedAggressiveCleanup:
      // trigger cleanup
      break;

    case HomeDirs::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free disk space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  auto now = platform_->GetCurrentTime();

  if (last_free_disk_space_) {
    auto diff = now - *last_free_disk_space_;

    ReportTimeBetweenFreeDiskSpace(diff.InSeconds());
  }

  last_free_disk_space_ = now;

  base::ElapsedTimer total_timer;

  FreeDiskSpaceInternal();

  ReportFreeDiskSpaceTotalTime(total_timer.Elapsed().InMilliseconds());

  auto after_cleanup = AmountOfFreeDiskSpace();
  if (!after_cleanup) {
    LOG(ERROR) << "Failed to get the amount of free disk space";
    return;
  }

  ReportFreeDiskSpaceTotalFreedInMb(
      MAX(0, after_cleanup.value() - free_space.value()) / 1024 / 1024);

  LOG(INFO) << "Disk cleanup complete.";
}

void HomeDirs::FreeDiskSpaceInternal() {
  auto homedirs = GetHomeDirs();
  auto unmounted_homedirs = homedirs;
  FilterMountedHomedirs(&unmounted_homedirs);

  // If ephemeral users are enabled, remove all cryptohomes except those
  // currently mounted or belonging to the owner.
  // |AreEphemeralUsers| will reload the policy to guarantee freshness.
  if (AreEphemeralUsersEnabled()) {
    RemoveNonOwnerCryptohomesInternal(homedirs);
    ReportDiskCleanupProgress(
        DiskCleanupProgress::kEphemeralUserProfilesCleaned);
    return;
  }

  // Clean Cache directories for every user (except current one).
  for (const auto& dir : unmounted_homedirs) {
    HomeDirs::DeleteCacheCallback(dir.shadow);
  }

  auto freeDiskSpace = AmountOfFreeDiskSpace();
  if (!freeDiskSpace) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return;
  }

  if (GetFreeDiskSpaceState(freeDiskSpace)
        == HomeDirs::FreeSpaceState::kAboveTarget) {
    ReportDiskCleanupProgress(
        DiskCleanupProgress::kBrowserCacheCleanedAboveTarget);
    return;
  }

  // Clean GCache directories for every user (except current one).
  for (const auto& dir : unmounted_homedirs) {
    HomeDirs::DeleteGCacheTmpCallback(dir.shadow);
  }

  const auto old_free_disk_space = freeDiskSpace;
  freeDiskSpace = AmountOfFreeDiskSpace();
  if (!freeDiskSpace) {
    LOG(ERROR) << "Failed to get the amount of free space";
    return;
  }

  const int64_t freed_gcache_space = freeDiskSpace.value() -
    old_free_disk_space.value();
  // Report only if something was deleted.
  if (freed_gcache_space > 0) {
    ReportFreedGCacheDiskSpaceInMb(freed_gcache_space / 1024 / 1024);
  }

  switch (GetFreeDiskSpaceState(freeDiskSpace)) {
    case HomeDirs::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
        DiskCleanupProgress::kGoogleDriveCacheCleanedAboveTarget);
      return;
    case HomeDirs::FreeSpaceState::kAboveThreshold:
    case HomeDirs::FreeSpaceState::kNeedNormalCleanup:
      ReportDiskCleanupProgress(
        DiskCleanupProgress::kGoogleDriveCacheCleanedAboveMinimum);
      return;
    case HomeDirs::FreeSpaceState::kNeedAggressiveCleanup:
      // continue cleanup
      break;
    case HomeDirs::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  // Clean Android cache directories for every user (except current one).
  for (const auto& dir : unmounted_homedirs) {
    HomeDirs::DeleteAndroidCacheCallback(dir.shadow);
  }

  switch (GetFreeDiskSpaceState()) {
    case HomeDirs::FreeSpaceState::kAboveTarget:
      ReportDiskCleanupProgress(
        DiskCleanupProgress::kAndroidCacheCleanedAboveTarget);
      return;
    case HomeDirs::FreeSpaceState::kAboveThreshold:
    case HomeDirs::FreeSpaceState::kNeedNormalCleanup:
      ReportDiskCleanupProgress(
        DiskCleanupProgress::kAndroidCacheCleanedAboveMinimum);
      return;
    case HomeDirs::FreeSpaceState::kNeedAggressiveCleanup:
      // continue cleanup
      break;
    case HomeDirs::FreeSpaceState::kError:
      LOG(ERROR) << "Failed to get the amount of free space";
      return;
    default:
      LOG(ERROR) << "Unhandled free disk state";
      return;
  }

  const int deleted_users_count = DeleteUserProfiles(homedirs);
  if (deleted_users_count > 0) {
    ReportDeletedUserProfiles(deleted_users_count);
  }

  // We had a chance to delete a user only if any unmounted homes existed.
  if (unmounted_homedirs.size() > 0) {
    ReportDiskCleanupProgress(
        HasTargetFreeSpace()
            ? DiskCleanupProgress::kWholeUserProfilesCleanedAboveTarget
            : DiskCleanupProgress::kWholeUserProfilesCleaned);
  } else {
    ReportDiskCleanupProgress(DiskCleanupProgress::kNoUnmountedCryptohomes);
  }
}

int HomeDirs::DeleteUserProfiles(const std::vector<HomeDir>& homedirs) {
  int deleted_users_count = 0;

  // Initialize user timestamp cache if it has not been yet. This reads the
  // last-activity time from each homedir's SerializedVaultKeyset.  This value
  // is only updated in the value keyset on unmount and every 24 hrs, so a
  // currently logged in user probably doesn't have an up to date value. This
  // is okay, since we don't delete currently logged in homedirs anyway.  (See
  // Mount::UpdateCurrentUserActivityTimestamp()).
  if (!timestamp_cache_->initialized()) {
    timestamp_cache_->Initialize();
    for (const auto& dir : homedirs) {
      HomeDirs::AddUserTimestampToCacheCallback(dir.shadow);
    }
  }

  // Delete old users using timestamp_cache_, the oldest first.
  // Don't delete anyone if we don't know who the owner is.
  // For consumer devices, don't delete the device owner. Enterprise-enrolled
  // devices have no owner, so don't delete the last user.
  std::string owner;
  if (enterprise_owned_ || GetOwner(&owner)) {
    int mounted_cryptohomes =
        std::count_if(homedirs.begin(), homedirs.end(),
                      [](auto& dir) { return dir.is_mounted; });
    while (!timestamp_cache_->empty()) {
      base::Time deleted_timestamp = timestamp_cache_->oldest_known_timestamp();
      FilePath deleted_user_dir = timestamp_cache_->RemoveOldestUser();
      std::string obfuscated = deleted_user_dir.BaseName().value();

      if (enterprise_owned_) {
        // If mounted_cryptohomes== 0, then there were no mounted cryptohomes
        // and hence no logged in users.  Thus we want to skip the last user in
        // our list, since they were the most-recent user on the device.
        if (timestamp_cache_->empty() && mounted_cryptohomes == 0) {
          // Put this user back in the cache, since they shouldn't be
          // permanently skipped; they may not be most-recent the next
          // time we run, and then they should be a candidate for deletion.
          timestamp_cache_->AddExistingUser(deleted_user_dir,
                                            deleted_timestamp);

          LOG(INFO) << "Skipped deletion of the most recent device user.";
          break;
        }
      } else {
        if (obfuscated == owner) {
          // We should never delete the device owner, so we permanently skip
          // them by not adding them back to the cache.
          LOG(INFO) << "Skipped deletion of the device owner.";
          continue;
        }
      }

      if (platform_->IsDirectoryMounted(
            brillo::cryptohome::home::GetHashedUserPath(obfuscated))) {
        LOG(INFO) << "Attempt to delete currently logged in user. Skipped...";
      } else {
        LOG(INFO) << "Freeing disk space by deleting user "
                  << deleted_user_dir.value();
        RemoveLECredentials(deleted_user_dir.BaseName().value());
        platform_->DeleteFile(deleted_user_dir, true);
        ++deleted_users_count;
        if (HasTargetFreeSpace())
          break;
      }
    }
  }

  return deleted_users_count;
}

base::Optional<int64_t> HomeDirs::AmountOfFreeDiskSpace() const {
  int64_t free_space = platform_->AmountOfFreeDiskSpace(shadow_root_);

  if (free_space < 0) {
    return base::nullopt;
  } else {
    return free_space;
  }
}

HomeDirs::FreeSpaceState HomeDirs::GetFreeDiskSpaceState() const {
  return GetFreeDiskSpaceState(AmountOfFreeDiskSpace());
}

HomeDirs::FreeSpaceState HomeDirs::GetFreeDiskSpaceState(
    base::Optional<int64_t> free_disk_space) const {

  if (!free_disk_space) {
    return HomeDirs::FreeSpaceState::kError;
  }

  int64_t value = free_disk_space.value();
  if (value >= kTargetFreeSpaceAfterCleanup) {
    return HomeDirs::FreeSpaceState::kAboveTarget;
  } else if (value >= kFreeSpaceThresholdToTriggerCleanup) {
    return HomeDirs::FreeSpaceState::kAboveThreshold;
  } else if (value >= kFreeSpaceThresholdToTriggerAggressiveCleanup) {
    return HomeDirs::FreeSpaceState::kNeedNormalCleanup;
  } else {
    return HomeDirs::FreeSpaceState::kNeedAggressiveCleanup;
  }
}

bool HomeDirs::HasTargetFreeSpace() const {
  return GetFreeDiskSpaceState() == HomeDirs::FreeSpaceState::kAboveTarget;
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
  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
  return GetValidKeyset(creds, vk.get(), nullptr /* key_index */,
                        nullptr /* error */);
}

bool HomeDirs::GetValidKeyset(const Credentials& creds,
                              VaultKeyset* vk,
                              int* key_index,
                              MountError* error) {
  if (error)
    *error = MOUNT_ERROR_NONE;

  if (!vk) {
    if (error)
      *error = MOUNT_ERROR_FATAL;
    return false;
  }

  std::string owner;
  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);
  // |AreEphemeralUsers| will reload the policy to guarantee freshness.
  if (AreEphemeralUsersEnabled() && GetOwner(&owner) && obfuscated != owner) {
    if (error)
      *error = MOUNT_ERROR_FATAL;
    return false;
  }

  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    if (error)
      *error = MOUNT_ERROR_FATAL;
    return false;
  }

  SecureBlob passkey;
  creds.GetPasskey(&passkey);

  bool any_keyset_exists = false;
  CryptoError last_crypto_error = CryptoError::CE_NONE;
  for (int index : key_indices) {
    if (!vk->Load(GetVaultKeysetPath(obfuscated, index)))
      continue;
    any_keyset_exists = true;
    // Skip decrypt attempts if the label doesn't match.
    // Treat an empty creds label as a wildcard.
    if (!creds.key_data().label().empty() &&
        creds.key_data().label() !=
            GetSerializedKeysetLabel(vk->serialized(), index))
      continue;
    // Skip LE Credentials if not explicitly identified by a label, since we
    // don't want unnecessary wrong attempts.
    if (creds.key_data().label().empty() &&
        (vk->serialized().flags() & SerializedVaultKeyset::LE_CREDENTIAL))
      continue;
    bool is_pcr_extended =
        platform_->FileExists(base::FilePath(kLockedToSingleUserFile));
    if (vk->Decrypt(passkey, is_pcr_extended, &last_crypto_error)) {
      if (key_index)
        *key_index = index;
      return true;
    }
  }

  MountError local_error = MOUNT_ERROR_NONE;
  if (!any_keyset_exists) {
    LOG(ERROR) << "No parsable keysets found for " << obfuscated;
    local_error = MOUNT_ERROR_FATAL;
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
        local_error = MOUNT_ERROR_FATAL;
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
  return false;
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

VaultKeyset* HomeDirs::GetVaultKeyset(const std::string& obfuscated_username,
                                      const std::string& key_label) const {
  if (key_label.empty())
    return NULL;

  // Walk all indices to find a match.
  // We should move to label-derived suffixes to be efficient.
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated_username, &key_indices))
    return NULL;
  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
  for (int index : key_indices) {
    if (!LoadVaultKeysetForUser(obfuscated_username, index, vk.get())) {
      continue;
    }
    if (GetSerializedKeysetLabel(vk->serialized(), index) == key_label) {
      vk->set_legacy_index(index);
      return vk.release();
    }
  }
  return NULL;
}

// TODO(wad) Figure out how this might fit in with vault_keyset.cc
bool HomeDirs::GetVaultKeysets(const std::string& obfuscated,
                               std::vector<int>* keysets) const {
  CHECK(keysets);
  FilePath user_dir = shadow_root_.Append(obfuscated);

  std::unique_ptr<FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(user_dir, false,
                                   base::FileEnumerator::FILES));
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

  std::unique_ptr<FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(user_dir, false /* Not recursive. */,
                                   base::FileEnumerator::FILES));
  FilePath next_path;
  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
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
    if (!LoadVaultKeysetForUser(obfuscated_username, index, vk.get())) {
      continue;
    }
    labels->push_back(GetSerializedKeysetLabel(vk->serialized(), index));
  }

  return (labels->size() > 0);
}


bool HomeDirs::CheckAuthorizationSignature(const KeyData& existing_key_data,
                                           const Key& new_key,
                                           const std::string& signature) {
  // If the existing key doesn't require authorization, then there's no
  // work to be done.
  //
  // Note, only the first authorizaton_data is honored at present.
  if (!existing_key_data.authorization_data_size() ||
      !existing_key_data.authorization_data(0).has_type())
    return true;

  if (!new_key.data().has_revision()) {
    LOG(INFO) << "CheckAuthorizationSignature called with no revision";
    return false;
  }

  const KeyAuthorizationData* existing_auth_data =
      &existing_key_data.authorization_data(0);
  const KeyAuthorizationSecret* secret;
  switch (existing_auth_data->type()) {
  // The data is passed in the clear but authenticated with a shared
  // symmetric secret.
  case KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_HMACSHA256:
    // Ensure there is an accessible signing key. Only a single
    // secret is allowed until there is a reason to support more.
    secret = NULL;
    for (int secret_i = 0;
         secret_i < existing_auth_data->secrets_size();
         ++secret_i) {
      secret = &existing_auth_data->secrets(secret_i);
      if (secret->usage().sign() && !secret->wrapped())
        break;
      secret = NULL;  // Clear if the candidate doesn't match.
    }
    if (!secret) {
      LOG(ERROR) << "Could not find a valid signing key for HMACSHA256";
      return false;
    }
    break;
  // The data is passed encrypted and authenticated with dedicated
  // encrypting and signing symmetric keys.
  case KeyAuthorizationData::KEY_AUTHORIZATION_TYPE_AES256CBC_HMACSHA256:
    LOG(ERROR) << "KEY_AUTHORIZATION_TYPE_AES256CBC_HMACSHA256 not supported";
    return false;
  default:
    LOG(ERROR) << "Unknown KeyAuthorizationType seen";
    return false;
  }
  // Now we're only handling HMACSHA256.
  // Specifically, HMACSHA256 is meant for interoperating with a server-side
  // signed password change operation which only specifies the revision and
  // new passphrase.  That means that change fields must be filtered to limit
  // silent updates to fields.  At present, this is done after this call. If
  // the signed fields vary by KeyAuthorizationType in the future, it should
  // be done here.
  std::string changes_str;
  ac::chrome::managedaccounts::account::Secret new_secret;
  new_secret.set_revision(new_key.data().revision());
  new_secret.set_secret(new_key.secret());
  if (!new_secret.SerializeToString(&changes_str)) {
    LOG(ERROR) << "Failed to serialized the new key";
    return false;
  }
  // Compute the HMAC
  brillo::SecureBlob hmac_key(secret->symmetric_key());
  brillo::SecureBlob data(changes_str.begin(), changes_str.end());
  SecureBlob hmac = CryptoLib::HmacSha256(hmac_key, data);

  // Check the HMAC
  if (signature.length() != hmac.size() ||
      brillo::SecureMemcmp(signature.data(), hmac.data(),
                             std::min(signature.size(), hmac.size()))) {
    LOG(ERROR) << "Supplied authorization signature was invalid.";
    return false;
  }

  if (existing_key_data.has_revision() &&
      existing_key_data.revision() >= new_key.data().revision()) {
    LOG(ERROR) << "The supplied key revision was too old.";
    return false;
  }

  return true;
}

CryptohomeErrorCode HomeDirs::UpdateKeyset(
    const Credentials& credentials,
    const Key* key_changes,
    const std::string& authorization_signature) {
  const std::string obfuscated_username =
      credentials.GetObfuscatedUsername(system_salt_);
  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
  if (!GetValidKeyset(credentials, vk.get(), nullptr /* key_index */,
                      nullptr /* error */)) {
    // Differentiate between failure and non-existent.
    if (!credentials.key_data().label().empty()) {
      vk.reset(
          GetVaultKeyset(obfuscated_username, credentials.key_data().label()));
      if (!vk.get()) {
        LOG(WARNING) << "UpdateKeyset: key not found";
        return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_NOT_FOUND;
      }
    }
    LOG(WARNING) << "UpdateKeyset: invalid authentication provided";
    return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_FAILED;
  }

  SerializedVaultKeyset *key = vk->mutable_serialized();

  // Check the privileges to ensure Update is allowed.
  // [In practice, Add/Remove could be used to override if present.]
  bool authorized_update = false;
  if (key->has_key_data()) {
    authorized_update = key->key_data().privileges().authorized_update();
    if (!key->key_data().privileges().update() && !authorized_update) {
      LOG(WARNING) << "UpdateKeyset: no update() privilege";
      return CRYPTOHOME_ERROR_AUTHORIZATION_KEY_DENIED;
    }
  }

  // Check the signature first so the rest of the function is untouched.
  if (authorized_update) {
    if (authorization_signature.empty() ||
        !CheckAuthorizationSignature(key->key_data(),
                                     *key_changes,
                                     authorization_signature)) {
      LOG(INFO) << "Unauthorized update attempted";
      return CRYPTOHOME_ERROR_UPDATE_SIGNATURE_INVALID;
    }
  }

  // Walk through each field and update the value.
  KeyData* merged_data = key->mutable_key_data();

  // Note! Revisions aren't tracked in general.
  if (key_changes->data().has_revision()) {
    merged_data->set_revision(key_changes->data().revision());
  }

  // TODO(wad,dkrahn): Add privilege dropping.
  SecureBlob passkey;
  credentials.GetPasskey(&passkey);
  if (key_changes->has_secret()) {
    SecureBlob new_passkey(key_changes->secret().begin(),
                           key_changes->secret().end());
    passkey.swap(new_passkey);
  }

  // Only merge additional KeyData if the update is not restricted.
  if (!authorized_update) {
    if (key_changes->data().has_type()) {
      merged_data->set_type(key_changes->data().type());
    }
    if (key_changes->data().has_label()) {
      merged_data->set_label(key_changes->data().label());
    }
    // Do not allow authorized_updates to change their keys unless we add
    // a new signature type.  This can be done in the future by adding
    // the authorization_data() to the new key_data, and changing the
    // CheckAuthorizationSignature() to check for a compatible "upgrade".
    if (key_changes->data().authorization_data_size() > 0) {
      // Only the first will be merged for now.
      *(merged_data->add_authorization_data()) =
          key_changes->data().authorization_data(0);
    }
  }

  if (!vk->Encrypt(passkey, obfuscated_username) ||
      !vk->Save(vk->source_file())) {
    LOG(ERROR) << "Failed to encrypt and write the updated keyset";
    return CRYPTOHOME_ERROR_BACKING_STORE_FAILURE;
  }
  return CRYPTOHOME_ERROR_NOT_SET;
}

CryptohomeErrorCode HomeDirs::AddKeyset(
                         const Credentials& existing_credentials,
                         const SecureBlob& new_passkey,
                         const KeyData* new_data,  // NULLable
                         bool clobber,
                         int* index) {
  // TODO(wad) Determine how to best bubble up the failures MOUNT_ERROR
  //           encapsulate wrt the TPM behavior.
  std::string obfuscated = existing_credentials.GetObfuscatedUsername(
    system_salt_);

  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
  if (!GetValidKeyset(existing_credentials, vk.get(), nullptr /* key_index */,
                      nullptr /* error */)) {
    // Differentiate between failure and non-existent.
    if (!existing_credentials.key_data().label().empty()) {
      vk.reset(
          GetVaultKeyset(obfuscated, existing_credentials.key_data().label()));
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
    brillo::SecureBlob passkey;
    existing_credentials.GetPasskey(&passkey);
    if (!vk->Encrypt(passkey, obfuscated) || !vk->Save(vk->source_file())) {
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
  for ( ; new_index < kKeyFileMax; ++new_index) {
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
    std::unique_ptr<VaultKeyset> match(
        GetVaultKeyset(obfuscated, new_data->label()));
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

CryptohomeErrorCode HomeDirs::RemoveKeyset(
  const Credentials& credentials,
  const KeyData& key_data) {
  // This error condition should be caught by the caller.
  if (key_data.label().empty())
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;

  const std::string obfuscated =
      credentials.GetObfuscatedUsername(system_salt_);

  std::unique_ptr<VaultKeyset> remove_vk(
      GetVaultKeyset(obfuscated, key_data.label()));
  if (!remove_vk.get()) {
    LOG(WARNING) << "RemoveKeyset: key to remove not found";
    return CRYPTOHOME_ERROR_KEY_NOT_FOUND;
  }

  std::unique_ptr<VaultKeyset> vk(vault_keyset_factory()->New(
              platform_, crypto_));
  if (!GetValidKeyset(credentials, vk.get(), nullptr /* key_index */,
                      nullptr /* error */)) {
    // Differentiate between failure and non-existent.
    if (!credentials.key_data().label().empty()) {
      vk.reset(GetVaultKeyset(obfuscated, credentials.key_data().label()));
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
  return  CRYPTOHOME_ERROR_NOT_SET;
}

bool HomeDirs::ForceRemoveKeyset(const std::string& obfuscated, int index) {
  // Note, external callers should check credentials.
  if (index < 0 || index >= kKeyFileMax)
    return false;

  std::unique_ptr<VaultKeyset> vk(
      vault_keyset_factory()->New(platform_, crypto_));
  FilePath path = GetVaultKeysetPath(obfuscated, index);
  if (!vk->Load(path)) {
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
  return shadow_root_
    .Append(obfuscated)
    .Append(kKeyFile)
    .AddExtension(base::IntToString(index));
}

void HomeDirs::RemoveNonOwnerCryptohomesCallback(const FilePath& user_dir) {
  if (!enterprise_owned_) {  // Enterprise owned? Delete it all.
    std::string owner;
    if (!GetOwner(&owner) ||  // No owner? bail.
        // Don't delete the owner's cryptohome!
        // TODO(wad,ellyjones) Add GetUser*Path-helpers
        user_dir == shadow_root_.Append(owner))
    return;
  }
  // Once we're sure this is not the owner's cryptohome, delete it.
  RemoveLECredentials(user_dir.BaseName().value());
  platform_->DeleteFile(user_dir, true);
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
    HomeDirs::RemoveNonOwnerCryptohomesCallback(dir.shadow);
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

    dir.shadow = entry;

    const std::string obfuscated = entry.BaseName().value();

    if (!brillo::cryptohome::home::IsSanitizedUserName(obfuscated))
      continue;

    dir.user = brillo::cryptohome::home::GetHashedUserPath(obfuscated);

    if (!platform_->DirectoryExists(dir.user))
      continue;

    ret.push_back(dir);
  }

  std::vector<FilePath> user_paths;
  std::transform(ret.begin(), ret.end(), std::back_inserter(user_paths),
    [] (const HomeDirs::HomeDir& homedir) {
      return homedir.user;
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
  homedirs->erase(
      std::remove_if(
          homedirs->begin(), homedirs->end(),
          [](const HomeDirs::HomeDir& dir) { return dir.is_mounted; }),
      homedirs->end());
}

void HomeDirs::DeleteDirectoryContents(const FilePath& dir) {
  std::unique_ptr<FileEnumerator> subdir_enumerator(
    platform_->GetFileEnumerator(dir, false,
      base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES |
          base::FileEnumerator::SHOW_SYM_LINKS));
  for (FilePath subdir_path = subdir_enumerator->Next();
       !subdir_path.empty();
       subdir_path = subdir_enumerator->Next()) {
    platform_->DeleteFile(subdir_path, true);
  }
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

bool HomeDirs::GetTrackedDirectory(
    const FilePath& user_dir, const FilePath& tracked_dir_name, FilePath* out) {
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

bool HomeDirs::GetTrackedDirectoryForDirCrypto(
    const FilePath& mount_dir,
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
      if (platform_->HasExtendedFileAttribute(
              dir, kTrackedDirectoryNameAttribute)) {
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

void HomeDirs::DeleteCacheCallback(const FilePath& user_dir) {
  FilePath cache;
  if (!GetTrackedDirectory(
          user_dir, FilePath(kUserHomeSuffix).Append(kCacheDir), &cache)) {
    LOG(ERROR) << "Failed to locate the cache directory.";
    return;
  }
  LOG(WARNING) << "Deleting Cache " << cache.value();
  DeleteDirectoryContents(cache);
}

void HomeDirs::DeleteGCacheTmpCallback(const FilePath& user_dir) {
  // GCache dirs that can be completely removed on low space.
  const FilePath kRemovableGCacheDirs[] = {
      FilePath(kUserHomeSuffix)
          .Append(kGCacheDir)
          .Append(kGCacheVersion1Dir)
          .Append(kGCacheTmpDir),
  };

  for (const auto& dir : kRemovableGCacheDirs) {
    FilePath gcachetmp;
    if (!GetTrackedDirectory(user_dir, dir, &gcachetmp)) {
      LOG(ERROR) << "Failed to locate GCache temp directory " << dir.value();
      continue;
    }
    LOG(WARNING) << "Deleting GCache " << gcachetmp.value();
    DeleteDirectoryContents(gcachetmp);
  }

  // GCache dirs that contain files marked as removable.
  const FilePath kCleanableGCacheDirs[] = {
      FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion1Dir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion2Dir),
  };

  for (const auto& dir : kCleanableGCacheDirs) {
    FilePath gcache_dir;
    if (!GetTrackedDirectory(user_dir, dir, &gcache_dir)) {
      LOG(ERROR) << "Failed to locate GCache directory " << dir.value();
      continue;
    }

    LOG(WARNING) << "Cleaning removable files in " << gcache_dir.value();
    RemoveAllRemovableFiles(platform_, gcache_dir);
  }
}

void HomeDirs::DeleteAndroidCacheCallback(const FilePath& user_dir) {
  FilePath root;
  if (!GetTrackedDirectory(user_dir, FilePath(kRootHomeSuffix), &root)) {
    LOG(ERROR) << "Failed to locate the root directory.";
    return;
  }
  // Find the cache directory by walking under the root directory
  // and looking for AndroidCache xattr set. Alternatively for Android N the
  // pacakge directory stores the inode of the cache directory in the
  // kAndroidCacheInodeAttribute xattr. Data is stored under
  // root/android-data/data/data/<package name>/[code_]cache. It is not
  // desirable to make all package name directories unencrypted, they
  // are not marked as tracked directory.
  // TODO(crbug/625872): Mark root/android/data/data/ as pass through.

  // A set of parent directory/inode combinations.  We need the parent directory
  // as the inodes may have been re-used elsewhere if the cache directory was
  // deleted.
  std::set<std::pair<const FilePath, ino_t>> cache_inodes;
  std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(root, true,
                                   base::FileEnumerator::DIRECTORIES));
  FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    ino_t inode = file_enumerator->GetInfo().stat().st_ino;
    std::pair<const FilePath, ino_t> parent_inode_pair =
        std::make_pair(next_path.DirName(), inode);
    if (cache_inodes.find(parent_inode_pair) != cache_inodes.end() ||
        platform_->HasExtendedFileAttribute(next_path,
                                            kAndroidCacheFilesAttribute)) {
      LOG(WARNING) << "Deleting Android Cache " << next_path.value();
      std::vector<FilePath> entry_list;
      platform_->EnumerateDirectoryEntries(next_path, false, &entry_list);
      for (const FilePath& entry : entry_list)
        platform_->DeleteFile(entry, true);
      cache_inodes.erase(parent_inode_pair);
    }
    for (const char* attribute :
         {kAndroidCacheInodeAttribute, kAndroidCodeCacheInodeAttribute}) {
      if (platform_->HasExtendedFileAttribute(next_path, attribute)) {
        uint64_t inode;
        if (platform_->GetExtendedFileAttribute(next_path,
                                                attribute,
                                                reinterpret_cast<char*>(&inode),
                                                sizeof(inode))) {
          // Because FileEnumerator processes all entries in a directory before
          // continuing to sub-directories we can assume that the inode is added
          // here before the directory that has the inode is processed.
          cache_inodes.insert(std::make_pair(next_path, inode));
        }
      }
    }
  }
}

void HomeDirs::AddUserTimestampToCacheCallback(const FilePath& user_dir) {
  const std::string obfuscated_username = user_dir.BaseName().value();
  //  Add a timestamp for every key.
  std::vector<int> key_indices;
  // Failure is okay since the loop falls through.
  GetVaultKeysets(obfuscated_username, &key_indices);
  std::unique_ptr<VaultKeyset> keyset(
      vault_keyset_factory()->New(platform_, crypto_));
  // Collect the most recent time for a given user by walking all
  // vaults.  This avoids trying to keep them in sync atomically.
  // TODO(wad,?) Move non-key vault metadata to a standalone file.
  base::Time timestamp = base::Time();
  for (int index : key_indices) {
    if (LoadVaultKeysetForUser(obfuscated_username, index, keyset.get()) &&
        keyset->serialized().has_last_activity_timestamp()) {
      const base::Time t = base::Time::FromInternalValue(
          keyset->serialized().last_activity_timestamp());
      if (t > timestamp)
        timestamp = t;
    }
  }
  if (!timestamp.is_null()) {
      timestamp_cache_->AddExistingUser(user_dir, timestamp);
  } else {
      timestamp_cache_->AddExistingUserNotime(user_dir);
  }
}

bool HomeDirs::LoadVaultKeysetForUser(const std::string& obfuscated_user,
                                      int index,
                                      VaultKeyset* keyset) const {
  // Load the encrypted keyset
  FilePath user_key_file = GetVaultKeysetPath(obfuscated_user, index);
  // We don't have keys yet, so just load it.
  // TODO(wad) Move to passing around keysets and not serialized versions.
  if (!keyset->Load(user_key_file)) {
    LOG(ERROR) << "Failed to read keyset file for user " << obfuscated_user;
    return false;
  }
  return true;
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

  if (!GetSystemSalt(NULL))
    return false;
  *owner = BuildObfuscatedUsername(plain_owner, system_salt_);
  return true;
}

bool HomeDirs::IsOrWillBeOwner(const std::string& account_id) {
  std::string owner;
  GetPlainOwner(&owner);
  return !enterprise_owned_ && (owner.empty() || account_id == owner);
}

bool HomeDirs::GetSystemSalt(SecureBlob* blob) {
  FilePath salt_file = shadow_root_.Append(kSystemSaltFile);
  if (!crypto_->GetOrCreateSalt(salt_file, CRYPTOHOME_DEFAULT_SALT_LENGTH,
                                false, &system_salt_)) {
    LOG(ERROR) << "Failed to create system salt.";
    return false;
  }
  if (blob)
    *blob = system_salt_;
  return true;
}

bool HomeDirs::Remove(const std::string& username) {
  std::string obfuscated = BuildObfuscatedUsername(username, system_salt_);
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
      BuildObfuscatedUsername(account_id_from, system_salt_);
  const std::string obfuscated_to =
      BuildObfuscatedUsername(account_id_to, system_salt_);

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
            << "(exists=" << base::PathExists(user_dir_from) << ") "
            << "=> '" << user_dir_to.value() << "' "
            << "(exists=" << base::PathExists(user_dir_to) << "); "
            << "renaming '" << user_path_from.value() << "' "
            << "(exists=" << base::PathExists(user_path_from) << ") "
            << "=> '" << user_path_to.value() << "' "
            << "(exists=" << base::PathExists(user_path_to) << "); "
            << "renaming '" << root_path_from.value() << "' "
            << "(exists=" << base::PathExists(root_path_from) << ") "
            << "=> '" << root_path_to.value() << "' "
            << "(exists=" << base::PathExists(root_path_to) << "); "
            << "renaming '" << new_user_path_from.value() << "' "
            << "(exists=" << base::PathExists(new_user_path_from) << ") "
            << "=> '" << new_user_path_to.value() << "' "
            << "(exists=" << base::PathExists(new_user_path_to) << ")";

  const bool already_renamed = !base::PathExists(user_dir_from);

  if (already_renamed) {
    LOG(INFO) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
              << account_id_to << "'): Consider already renamed. "
              << "('" << user_dir_from.value() << "' doesn't exist.)";
    return true;
  }

  const bool can_rename = !base::PathExists(user_dir_to);

  if (!can_rename) {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): Destination already exists! "
               << " '" << user_dir_from.value() << "' "
               << "(exists=" << base::PathExists(user_dir_from) << ") "
               << "=> '" << user_dir_to.value() << "' "
               << "(exists=" << base::PathExists(user_dir_to) << "); ";
    return false;
  }

  // |user_dir_renamed| is return value, because two other directories are
  // empty and will be created as needed.
  const bool user_dir_renamed =
      !base::PathExists(user_dir_from) ||
      platform_->Rename(user_dir_from, user_dir_to);

  if (user_dir_renamed) {
    const bool user_path_renamed =
        !base::PathExists(user_path_from) ||
        platform_->Rename(user_path_from, user_path_to);
    const bool root_path_renamed =
        !base::PathExists(root_path_from) ||
        platform_->Rename(root_path_from, root_path_to);
    const bool new_user_path_renamed =
        !base::PathExists(new_user_path_from) ||
        platform_->Rename(new_user_path_from, new_user_path_to);
    if (!user_path_renamed) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to rename user_path.";
    }
    if (!root_path_renamed) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to rename root_path.";
    }
    if (!new_user_path_renamed) {
      LOG(WARNING) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
                   << account_id_to << "'): failed to rename new_user_path.";
    }
  } else {
    LOG(ERROR) << "HomeDirs::Rename(from='" << account_id_from << "', to='"
               << account_id_to << "'): failed to rename user_dir.";
  }

  return user_dir_renamed;
}

int64_t HomeDirs::ComputeSize(const std::string& account_id) {
  // BuildObfuscatedUsername below doesn't accept empty username.
  if (account_id.empty()) {
    // Empty account is always non-existent, return 0 as specified.
    return 0;
  }

  std::string obfuscated = BuildObfuscatedUsername(account_id, system_salt_);
  FilePath user_dir = FilePath(shadow_root_).Append(obfuscated);
  FilePath user_path = brillo::cryptohome::home::GetUserPath(account_id);
  FilePath root_path = brillo::cryptohome::home::GetRootPath(account_id);
  int64_t total_size = 0;
  int64_t size = platform_->ComputeDirectorySize(user_dir);
  if (size > 0) {
    total_size += size;
  }
  size = platform_->ComputeDirectorySize(user_path);
  if (size > 0) {
    total_size += size;
  }
  size = platform_->ComputeDirectorySize(root_path);
  if (size > 0) {
    total_size += size;
  }
  return total_size;
}

bool HomeDirs::Migrate(const Credentials& newcreds,
                       const SecureBlob& oldkey,
                       scoped_refptr<Mount> user_mount) {
  SecureBlob newkey;
  newcreds.GetPasskey(&newkey);
  Credentials oldcreds(newcreds.username().c_str(), oldkey);
  std::string obfuscated = newcreds.GetObfuscatedUsername(system_salt_);
  if (!user_mount) {
    user_mount = mount_factory_->New();
    if (!user_mount->Init(platform_, crypto_, timestamp_cache_,
                          base::DoNothing())) {
      LOG(ERROR) << "Migrate: Init mount failed";
      return false;
    }
  }
  base::ScopedClosureRunner scoped_unmount_runner;
  if (!user_mount->IsMounted()) {
    if (!user_mount->MountCryptohome(oldcreds, Mount::MountArgs(), NULL)) {
      LOG(ERROR) << "Migrate: Mount failed";
      // Fail as early as possible. Note that we don't have to worry about
      // leaking this mount - Mount unmounts itself if it's still mounted in the
      // destructor.
      return false;
    }
    // We've mounted it, so we'll need to unmount it.
    auto unmount_closure = base::BindOnce(
        [](scoped_refptr<Mount> user_mount) {
          if (user_mount->UnmountCryptohome()) {
            LOG(INFO) << "Unmounted cryptohome after migrating.";
          } else {
            LOG(ERROR) << "Failed to unmount cryptohome after migrating.";
          }
        },
        user_mount);
    scoped_unmount_runner =
        base::ScopedClosureRunner(std::move(unmount_closure));
  }
  int key_index = user_mount->CurrentKey();
  if (key_index == -1) {
    LOG(ERROR) << "Attempted migration of key-less mount.";
    return false;
  }

  // Grab the current key and check its permissions early.
  // add() and remove() are required.  mount() was checked
  // already during MountCryptohome().
  std::unique_ptr<VaultKeyset> vk(
    vault_keyset_factory()->New(platform_, crypto_));
  if (!LoadVaultKeysetForUser(obfuscated, key_index, vk.get())) {
    LOG(ERROR) << "Migrate: failed to reload the active keyset";
    return false;
  }
  const KeyData *key_data = NULL;
  if (vk->serialized().has_key_data()) {
    key_data = &(vk->serialized().key_data());
    // legacy keys are full privs
    if (!vk->serialized().key_data().privileges().add() ||
        !vk->serialized().key_data().privileges().remove()) {
      LOG(ERROR) << "Migrate: key lacks sufficient privileges()";
      return false;
    }
  }

  if (vk->serialized().has_wrapped_chaps_key()) {
    // Note that chaps_key in vault keyset is migrated/copied over into the new
    // keyset in AddKeyset().
    LOG(INFO) << "Keeping the same chaps token auth data in vault keyset.";
  } else {
    LOG(INFO) << "Changing the token auth data for legacy chaps key.";

    SecureBlob old_auth_data;
    SecureBlob auth_data;
    std::string username = newcreds.username();
    FilePath salt_file = GetChapsTokenSaltPath(username);
    if (!crypto_->PasskeyToTokenAuthData(newkey, salt_file, &auth_data) ||
        !crypto_->PasskeyToTokenAuthData(oldkey, salt_file, &old_auth_data)) {
      // On failure, token data may be partially migrated. Ideally, the user
      // will re-attempt with the same passphrase.
      return false;
    }
    chaps_client_.ChangeTokenAuthData(GetChapsTokenDir(username), old_auth_data,
                                      auth_data);
  }

  int new_key_index = -1;
  // For a labeled key with the same label as the old key,
  //  this will overwrite the existing keyset file.
  if (AddKeyset(oldcreds, newkey, key_data, true, &new_key_index) !=
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

  if (!user_mount->SetUserCreds(newcreds, key_index))
    LOG(WARNING) << "Failed to set new creds";

  return true;
}

namespace {
  const char *kChapsDaemonName = "chaps";
  const char *kChapsDirName = ".chaps";
  const char *kChapsSaltName = "auth_data_salt";
}

FilePath HomeDirs::GetChapsTokenDir(const std::string& user) const {
  return brillo::cryptohome::home::GetDaemonPath(user, kChapsDaemonName);
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
  std::unique_ptr<VaultKeyset> vk(
      vault_keyset_factory()->New(platform_, crypto_));

  std::string obfuscated = creds.GetObfuscatedUsername(system_salt_);
  std::vector<int> key_indices;
  if (!GetVaultKeysets(obfuscated, &key_indices)) {
    LOG(WARNING) << "No valid keysets on disk for " << obfuscated;
    return;
  }

  bool credentials_checked = false;
  std::unique_ptr<VaultKeyset> vk_reset(
      vault_keyset_factory()->New(platform_, crypto_));
  for (int index : key_indices) {
    if (!vk_reset->Load(GetVaultKeysetPath(obfuscated, index)))
      continue;
    // Skip non-LE Credentials.
    if (!vk_reset->IsLECredential())
      continue;
    if (crypto_->GetWrongAuthAttempts(vk_reset->serialized()) == 0)
      continue;

    if (!credentials_checked) {
      // Make sure the credential can actually be used for sign-in.
      // It is also the easiest way to get a valid keyset.
      if (!GetValidKeyset(creds, vk.get(), nullptr /* key_index */,
                          nullptr /* error */)) {
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
      vk_reset->mutable_serialized()->mutable_key_data()->mutable_policy()
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

  std::unique_ptr<VaultKeyset> vk_remove(
      vault_keyset_factory()->New(platform_, crypto_));
  for (int index : key_indices) {
    base::FilePath vk_path = GetVaultKeysetPath(obfuscated_username, index);
    if (!vk_remove->Load(vk_path))
      continue;
    // Skip non-LE Credentials.
    if (!vk_remove->IsLECredential())
      continue;

    uint64_t label = vk_remove->serialized().le_label();
    if (!crypto_->RemoveLECredential(label)) {
      LOG(WARNING) << "Failed to remove an LE credential, label: " << label;
      continue;
    }

    // Remove the cryptohome VaultKeyset data.
    platform_->DeleteFile(vk_path, true);
  }
}

int32_t HomeDirs::GetUnmountedAndroidDataCount() {
  const auto homedirs = GetHomeDirs();

  return std::count_if(
      homedirs.begin(), homedirs.end(), [&](const HomeDirs::HomeDir& dir) {
        if (dir.is_mounted)
          return false;

        const std::string obfuscated = dir.shadow.BaseName().value();

        if (EcryptfsCryptohomeExists(obfuscated))
          return false;

        FilePath root_home_dir;
        return GetTrackedDirectory(dir.shadow, FilePath(kRootHomeSuffix),
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
