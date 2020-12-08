// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/homedirs.h"

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
#include "cryptohome/keyset_management.h"
#include "cryptohome/platform.h"
#include "cryptohome/signed_secret.pb.h"
#include "cryptohome/storage/mount_helper.h"
#include "cryptohome/storage/user_oldest_activity_timestamp_cache.h"
#include "cryptohome/vault_keyset.h"

using base::FilePath;
using brillo::SecureBlob;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {

const char* kEmptyOwner = "";
// Each xattr is set to Android app internal data directory, contains
// 8-byte inode number of cache subdirectory.  See
// frameworks/base/core/java/android/app/ContextImpl.java
const char kAndroidCacheInodeAttribute[] = "user.inode_cache";
const char kAndroidCodeCacheInodeAttribute[] = "user.inode_code_cache";
const char kTrackedDirectoryNameAttribute[] = "user.TrackedDirectoryName";
const char kRemovableFileAttribute[] = "user.GCacheRemovable";

HomeDirs::HomeDirs(Platform* platform,
                   KeysetManagement* keyset_management,
                   const brillo::SecureBlob& system_salt,
                   UserOldestActivityTimestampCache* timestamp_cache,
                   std::unique_ptr<policy::PolicyProvider> policy_provider)
    : platform_(platform),
      keyset_management_(keyset_management),
      system_salt_(system_salt),
      timestamp_cache_(timestamp_cache),
      policy_provider_(std::move(policy_provider)),
      enterprise_owned_(false) {}

HomeDirs::~HomeDirs() {}

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

bool HomeDirs::SetLockedToSingleUser() const {
  return platform_->TouchFileDurable(base::FilePath(kLockedToSingleUserFile));
}

bool HomeDirs::Exists(const std::string& obfuscated_username) const {
  FilePath user_dir = ShadowRoot().Append(obfuscated_username);
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

  base::FilePath ts_file = UserActivityTimestampPath(obfuscated, index);
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

void HomeDirs::RemoveNonOwnerCryptohomesCallback(
    const std::string& obfuscated) {
  if (!enterprise_owned_) {  // Enterprise owned? Delete it all.
    std::string owner;
    if (!GetOwner(&owner) || obfuscated == owner)
      return;
  }
  // Once we're sure this is not the owner's cryptohome, delete it.
  keyset_management_->RemoveLECredentials(obfuscated);
  FilePath shadow_dir = ShadowRoot().Append(obfuscated);
  platform_->DeletePathRecursively(shadow_dir);
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
  if (!platform_->EnumerateDirectoryEntries(ShadowRoot(), false, &entries)) {
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
    platform_->DeletePathRecursively(dirent);
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
  keyset_management_->GetVaultKeysets(obfuscated, &key_indices);
  // Collect the most recent time for a given user by walking all
  // vaults.  This avoids trying to keep them in sync atomically.
  // TODO(wad,?) Move non-key vault metadata to a standalone file.
  base::Time timestamp = base::Time();
  for (int index : key_indices) {
    std::unique_ptr<VaultKeyset> keyset =
        keyset_management_->LoadVaultKeysetForUser(obfuscated, index);
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
  FilePath user_dir = ShadowRoot().Append(obfuscated_username);
  if (!platform_->CreateDirectory(user_dir)) {
    return false;
  }

  return true;
}

bool HomeDirs::Remove(const std::string& username) {
  std::string obfuscated = SanitizeUserNameWithSalt(username, system_salt_);
  keyset_management_->RemoveLECredentials(obfuscated);

  FilePath user_dir = ShadowRoot().Append(obfuscated);
  FilePath user_path = brillo::cryptohome::home::GetUserPath(username);
  FilePath root_path = brillo::cryptohome::home::GetRootPath(username);
  return platform_->DeletePathRecursively(user_dir) &&
         platform_->DeletePathRecursively(user_path) &&
         platform_->DeletePathRecursively(root_path);
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

  const FilePath user_dir_from = ShadowRoot().Append(obfuscated_from);
  const FilePath user_path_from =
      brillo::cryptohome::home::GetUserPath(account_id_from);
  const FilePath root_path_from =
      brillo::cryptohome::home::GetRootPath(account_id_from);
  const FilePath new_user_path_from =
      FilePath(MountHelper::GetNewUserPath(account_id_from));

  const FilePath user_dir_to = ShadowRoot().Append(obfuscated_to);
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
    const bool user_path_deleted =
        platform_->DeletePathRecursively(user_path_from);
    const bool root_path_deleted =
        platform_->DeletePathRecursively(root_path_from);
    const bool new_user_path_deleted =
        platform_->DeletePathRecursively(new_user_path_from);
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
  FilePath user_dir = ShadowRoot().Append(obfuscated);

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
      platform_->GetDirCryptoKeyState(ShadowRoot());
  if (state == dircrypto::KeyState::UNKNOWN ||
      state == dircrypto::KeyState::NOT_SUPPORTED) {
    return false;
  }

  // Use the existence of eCryptfs vault as a single of whether the user needs
  // dircrypto migration. eCryptfs test is adapted from
  // Mount::DoesEcryptfsCryptohomeExist.
  const FilePath user_ecryptfs_vault_dir =
      ShadowRoot().Append(obfuscated_username).Append(kEcryptfsVaultDir);
  return platform_->DirectoryExists(user_ecryptfs_vault_dir);
}

int32_t HomeDirs::GetUnmountedAndroidDataCount() {
  const auto homedirs = GetHomeDirs();

  return std::count_if(
      homedirs.begin(), homedirs.end(), [&](const HomeDirs::HomeDir& dir) {
        if (dir.is_mounted)
          return false;

        if (EcryptfsCryptohomeExists(dir.obfuscated))
          return false;

        FilePath shadow_dir = ShadowRoot().Append(dir.obfuscated);
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
