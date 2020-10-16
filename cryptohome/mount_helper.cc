// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_helper.h"

#include <sys/stat.h>

#include <memory>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/homedirs.h"
#include "cryptohome/mount_constants.h"

using base::FilePath;
using base::StringPrintf;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace {
constexpr uid_t kMountOwnerUid = 0;
constexpr gid_t kMountOwnerGid = 0;
constexpr gid_t kDaemonStoreGid = 400;

const int kDefaultEcryptfsKeySize = CRYPTOHOME_AES_KEY_BYTES;

FilePath GetUserEphemeralMountDirectory(
    const std::string& obfuscated_username) {
  return FilePath(cryptohome::kEphemeralCryptohomeDir)
      .Append(cryptohome::kEphemeralMountDir)
      .Append(obfuscated_username);
}

FilePath GetMountedEphemeralRootHomePath(
    const std::string& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(cryptohome::kRootHomeSuffix);
}

FilePath GetMountedEphemeralUserHomePath(
    const std::string& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(cryptohome::kUserHomeSuffix);
}

FilePath VaultPathToUserPath(const FilePath& vault) {
  return vault.Append(cryptohome::kUserHomeSuffix);
}

FilePath VaultPathToRootPath(const FilePath& vault) {
  return vault.Append(cryptohome::kRootHomeSuffix);
}

}  // namespace

namespace cryptohome {

const char kDefaultHomeDir[] = "/home/chronos/user";

std::vector<FilePath> MountHelper::GetTrackedSubdirectories() {
  return std::vector<FilePath>{
      FilePath(kRootHomeSuffix),
      FilePath(kUserHomeSuffix),
      FilePath(kUserHomeSuffix).Append(kCacheDir),
      FilePath(kUserHomeSuffix).Append(kDownloadsDir),
      FilePath(kUserHomeSuffix).Append(kMyFilesDir),
      FilePath(kUserHomeSuffix).Append(kMyFilesDir).Append(kDownloadsDir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion1Dir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion2Dir),
      FilePath(kUserHomeSuffix)
          .Append(kGCacheDir)
          .Append(kGCacheVersion1Dir)
          .Append(kGCacheBlobsDir),
      FilePath(kUserHomeSuffix)
          .Append(kGCacheDir)
          .Append(kGCacheVersion1Dir)
          .Append(kGCacheTmpDir),
  };
}

// static
FilePath MountHelper::GetNewUserPath(const std::string& username) {
  std::string sanitized = SanitizeUserName(username);
  std::string user_dir = StringPrintf("u-%s", sanitized.c_str());
  return FilePath("/home")
      .Append(cryptohome::kDefaultSharedUser)
      .Append(user_dir);
}

// static
FilePath MountHelper::GetEphemeralSparseFile(
    const std::string& obfuscated_username) {
  return FilePath(cryptohome::kEphemeralCryptohomeDir)
      .Append(kSparseFileDir)
      .Append(obfuscated_username);
}

FilePath MountHelper::GetUserTemporaryMountDirectory(
    const std::string& obfuscated_username) const {
  return shadow_root_.Append(obfuscated_username).Append(kTemporaryMountDir);
}

FilePath MountHelper::GetMountedUserHomePath(
    const std::string& obfuscated_username) const {
  return HomeDirs::GetUserMountDirectory(shadow_root_, obfuscated_username)
      .Append(kUserHomeSuffix);
}

FilePath MountHelper::GetMountedRootHomePath(
    const std::string& obfuscated_username) const {
  return HomeDirs::GetUserMountDirectory(shadow_root_, obfuscated_username)
      .Append(kRootHomeSuffix);
}

bool MountHelper::EnsurePathComponent(const FilePath& path,
                                      size_t num,
                                      uid_t uid,
                                      gid_t gid) const {
  std::vector<std::string> path_parts;
  path.GetComponents(&path_parts);
  FilePath check_path(path_parts[0]);
  for (size_t i = 1; i < num; i++)
    check_path = check_path.Append(path_parts[i]);

  base::stat_wrapper_t st;
  if (!platform_->Stat(check_path, &st)) {
    // Dirent not there, so create and set ownership.
    if (!platform_->CreateDirectory(check_path)) {
      PLOG(ERROR) << "Can't create: " << check_path.value();
      return false;
    }
    if (!platform_->SetOwnership(check_path, uid, gid, true)) {
      PLOG(ERROR) << "Can't chown/chgrp: " << check_path.value() << " uid "
                  << uid << " gid " << gid;
      return false;
    }
  } else {
    // Dirent there; make sure it's acceptable.
    if (!S_ISDIR(st.st_mode)) {
      LOG(ERROR) << "Non-directory path: " << check_path.value();
      return false;
    }
    if (st.st_uid != uid) {
      LOG(ERROR) << "Owner mismatch: " << check_path.value() << " " << st.st_uid
                 << " != " << uid;
      return false;
    }
    if (st.st_gid != gid) {
      LOG(ERROR) << "Group mismatch: " << check_path.value() << " " << st.st_gid
                 << " != " << gid;
      return false;
    }
    if (st.st_mode & S_IWOTH) {
      LOG(ERROR) << "Permissions too lenient: " << check_path.value() << " has "
                 << std::oct << st.st_mode;
      return false;
    }
  }
  return true;
}

bool MountHelper::EnsureDirHasOwner(const FilePath& dir,
                                    uid_t desired_uid,
                                    gid_t desired_gid) const {
  std::vector<std::string> path_parts;
  dir.GetComponents(&path_parts);
  // The path given should be absolute to that its first part is /. This is not
  // actually checked so that relative paths can be used during testing.
  for (size_t i = 2; i <= path_parts.size(); i++) {
    bool last = (i == path_parts.size());
    uid_t uid = last ? desired_uid : kMountOwnerUid;
    gid_t gid = last ? desired_gid : kMountOwnerGid;
    if (!EnsurePathComponent(dir, i, uid, gid))
      return false;
  }
  return true;
}

bool MountHelper::EnsureNewUserDirExists(const std::string& username) const {
  FilePath dir(GetNewUserPath(username));
  if (!EnsureDirHasOwner(dir.DirName(), default_uid_, default_gid_)) {
    LOG(ERROR) << "EnsureDirHasOwner() failed: " << dir.value();
    return false;
  }
  if (!platform_->CreateDirectory(dir)) {
    // chronos can modify the contents of /home/chronos.
    // Try deleting the file or link at /home/chronos/u-$hash to be robust
    // against malicious code running as chronos.
    if (!platform_->DeleteFile(dir, /*recursive=*/false)) {
      LOG(ERROR) << "DeleteFile() failed: " << dir.value();
      return false;
    }
    // Try again.
    if (!platform_->CreateDirectory(dir)) {
      LOG(ERROR) << "CreateDirectory() failed: " << dir.value();
      return false;
    }
  }
  return true;
}

void MountHelper::MigrateToUserHome(const FilePath& vault_path) const {
  std::vector<FilePath> ent_list;
  FilePath user_path(VaultPathToUserPath(vault_path));
  FilePath root_path(VaultPathToRootPath(vault_path));
  base::stat_wrapper_t st;

  // This check makes the migration idempotent; if we completed a migration,
  // root_path will exist and we're done, and if we didn't complete it, we can
  // finish it.
  if (platform_->Stat(root_path, &st) && S_ISDIR(st.st_mode) &&
      st.st_mode & S_ISVTX && st.st_uid == kMountOwnerUid &&
      st.st_gid == kDaemonStoreGid) {
    return;
  }

  // There are three ways to get here:
  // 1) the Stat() call above succeeded, but what we saw was not a root-owned
  //    directory.
  // 2) the Stat() call above failed with -ENOENT
  // 3) the Stat() call above failed for some other reason
  // In any of these cases, it is safe for us to rm root_path, since the only
  // way it could have gotten there is if someone undertook some funny business
  // as root.
  platform_->DeleteFile(root_path, true);

  // Get the list of entries before we create user_path, since user_path will be
  // inside dir.
  platform_->EnumerateDirectoryEntries(vault_path, false, &ent_list);

  if (!platform_->CreateDirectory(user_path)) {
    PLOG(ERROR) << "CreateDirectory() failed: " << user_path.value();
    return;
  }

  if (!platform_->SetOwnership(user_path, default_uid_, default_gid_, true)) {
    PLOG(ERROR) << "SetOwnership() failed: " << user_path.value();
    return;
  }

  for (const auto& ent : ent_list) {
    FilePath basename(ent);
    FilePath next_path = basename;
    basename = basename.BaseName();
    // Don't move the user/ directory itself. We're currently operating on an
    // _unmounted_ ecryptfs, which means all the filenames are encrypted except
    // the user and root passthrough directories.
    if (basename.value() == kUserHomeSuffix) {
      LOG(WARNING) << "Interrupted migration detected.";
      continue;
    }
    FilePath dest_path(user_path);
    dest_path = dest_path.Append(basename);
    if (!platform_->Rename(next_path, dest_path)) {
      // TODO(ellyjones): UMA event log for this.
      PLOG(WARNING) << "Migration fault: can't move " << next_path.value()
                    << " to " << dest_path.value();
    }
  }
  // Create root_path at the end as a sentinel for migration.
  if (!platform_->CreateDirectory(root_path)) {
    PLOG(ERROR) << "CreateDirectory() failed: " << root_path.value();
    return;
  }
  if (!platform_->SetOwnership(root_path, kMountOwnerUid, kDaemonStoreGid,
                               true)) {
    PLOG(ERROR) << "SetOwnership() failed: " << root_path.value();
    return;
  }
  if (!platform_->SetPermissions(root_path, S_IRWXU | S_IRWXG | S_ISVTX)) {
    PLOG(ERROR) << "SetPermissions() failed: " << root_path.value();
    return;
  }
  LOG(INFO) << "Migrated (or created) user directory: " << vault_path.value();
}

bool MountHelper::EnsureUserMountPoints(const std::string& username) const {
  FilePath root_path = GetRootPath(username);
  FilePath user_path = GetUserPath(username);
  if (!EnsureDirHasOwner(root_path, kMountOwnerUid, kMountOwnerGid)) {
    LOG(ERROR) << "Couldn't ensure root path: " << root_path.value();
    return false;
  }
  if (!EnsureDirHasOwner(user_path, default_uid_, default_access_gid_)) {
    LOG(ERROR) << "Couldn't ensure user path: " << user_path.value();
    return false;
  }
  if (!EnsureNewUserDirExists(username)) {
    LOG(ERROR) << "Couldn't ensure temp path.";
    return false;
  }
  return true;
}

bool MountHelper::SetUpGroupAccess(const FilePath& home_dir) const {
  // Make the following directories group accessible by other system daemons:
  //   {home_dir}
  //   {home_dir}/Downloads
  //   {home_dir}/MyFiles
  //   {home_dir}/MyFiles/Downloads
  //   {home_dir}/GCache
  //   {home_dir}/GCache/v1 (only if it exists)
  //
  // Make the following directories group accessible and writable by other
  // system daemons:
  //   {home_dir}/GCache/v2
  const struct {
    FilePath path;
    bool optional = false;
    bool group_writable = false;
  } kGroupAccessiblePaths[] = {
      {home_dir},
      {home_dir.Append(kDownloadsDir)},
      {home_dir.Append(kMyFilesDir)},
      {home_dir.Append(kMyFilesDir).Append(kDownloadsDir)},
      {home_dir.Append(kGCacheDir)},
      {home_dir.Append(kGCacheDir).Append(kGCacheVersion1Dir), true},
      {home_dir.Append(kGCacheDir).Append(kGCacheVersion2Dir), false, true},
  };

  constexpr mode_t kDefaultMode = S_IXGRP;
  constexpr mode_t kWritableMode = kDefaultMode | S_IWGRP;
  for (const auto& accessible : kGroupAccessiblePaths) {
    if (!platform_->DirectoryExists(accessible.path)) {
      if (accessible.optional)
        continue;
      else
        return false;
    }

    if (!platform_->SetGroupAccessible(
            accessible.path, default_access_gid_,
            accessible.group_writable ? kWritableMode : kDefaultMode)) {
      return false;
    }
  }
  return true;
}

void MountHelper::RecursiveCopy(const FilePath& source,
                                const FilePath& destination) const {
  std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(source, false, base::FileEnumerator::FILES));
  FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    FilePath file_name = next_path.BaseName();
    FilePath destination_file = destination.Append(file_name);
    if (!platform_->Copy(next_path, destination_file) ||
        !platform_->SetOwnership(destination_file, default_uid_, default_gid_,
                                 true)) {
      LOG(ERROR) << "Couldn't change owner (" << default_uid_ << ":"
                 << default_gid_
                 << ") of destination path: " << destination_file.value();
    }
  }
  std::unique_ptr<cryptohome::FileEnumerator> dir_enumerator(
      platform_->GetFileEnumerator(source, false,
                                   base::FileEnumerator::DIRECTORIES));
  while (!(next_path = dir_enumerator->Next()).empty()) {
    FilePath dir_name = FilePath(next_path).BaseName();
    FilePath destination_dir = destination.Append(dir_name);
    VLOG(1) << "RecursiveCopy: " << destination_dir.value();
    if (!platform_->CreateDirectory(destination_dir) ||
        !platform_->SetOwnership(destination_dir, default_uid_, default_gid_,
                                 true)) {
      LOG(ERROR) << "Couldn't change owner (" << default_uid_ << ":"
                 << default_gid_
                 << ") of destination path: " << destination_dir.value();
    }
    RecursiveCopy(FilePath(next_path), destination_dir);
  }
}

void MountHelper::CopySkeleton(const FilePath& destination) const {
  RecursiveCopy(FilePath(skeleton_source_), destination);
}

bool MountHelper::SetUpEphemeralCryptohome(const FilePath& source_path) {
  CopySkeleton(source_path);

  // Create the Downloads, MyFiles, MyFiles/Downloads, GCache and GCache/v2
  // directories if they don't exist so they can be made group accessible when
  // SetUpGroupAccess() is called.
  const FilePath user_files_paths[] = {
      FilePath(source_path).Append(kDownloadsDir),
      FilePath(source_path).Append(kMyFilesDir),
      FilePath(source_path).Append(kMyFilesDir).Append(kDownloadsDir),
      FilePath(source_path).Append(kGCacheDir),
      FilePath(source_path).Append(kGCacheDir).Append(kGCacheVersion2Dir),
  };
  for (const auto& path : user_files_paths) {
    if (platform_->DirectoryExists(path))
      continue;

    if (!platform_->CreateDirectory(path) ||
        !platform_->SetOwnership(path, default_uid_, default_gid_, true)) {
      LOG(ERROR) << "Couldn't create user path directory: " << path.value();
      return false;
    }
  }

  if (!platform_->SetOwnership(source_path, default_uid_, default_access_gid_,
                               true)) {
    LOG(ERROR) << "Couldn't change owner (" << default_uid_ << ":"
               << default_access_gid_ << ") of path: " << source_path.value();
    return false;
  }

  if (!SetUpGroupAccess(source_path)) {
    return false;
  }

  return true;
}

bool MountHelper::MountLegacyHome(const FilePath& from) {
  VLOG(1) << "MountLegacyHome from " << from.value();
  // Multiple mounts can't live on the legacy mountpoint.
  if (platform_->IsDirectoryMounted(FilePath(kDefaultHomeDir))) {
    LOG(INFO) << "Skipping binding to /home/chronos/user";
    return true;
  }

  if (!BindAndPush(from, FilePath(kDefaultHomeDir)))
    return false;

  return true;
}

bool MountHelper::BindMyFilesDownloads(const base::FilePath& user_home) {
  if (!platform_->DirectoryExists(user_home)) {
    LOG(ERROR) << "Failed to bind MyFiles/Downloads, missing directory: "
               << user_home.value();
    return false;
  }

  const FilePath downloads = user_home.Append(kDownloadsDir);
  if (!platform_->DirectoryExists(downloads)) {
    LOG(ERROR) << "Failed to bind MyFiles/Downloads, missing directory: "
               << downloads.value();
    return false;
  }

  const FilePath downloads_in_myfiles =
      user_home.Append(kMyFilesDir).Append(kDownloadsDir);
  if (!platform_->DirectoryExists(downloads_in_myfiles)) {
    LOG(ERROR) << "Failed to bind MyFiles/Downloads, missing directory: "
               << downloads_in_myfiles.value();
    return false;
  }
  /*
   * User could have saved files in MyFiles/Downloads in case cryptohome
   * crashed and bind mounts were removed by error. See crbug.com/1080730.
   * Move the files back to Download unless a file already exits.
   */
  std::unique_ptr<FileEnumerator> enumerator(platform_->GetFileEnumerator(
      downloads_in_myfiles, false /* recursive */,
      base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES));
  bool warning_sent = false;
  for (FilePath obj = enumerator->Next(); !obj.empty();
       obj = enumerator->Next()) {
    FilePath obj_in_downloads = downloads.Append(obj.BaseName());

    if (platform_->FileExists(obj_in_downloads))
      platform_->DeleteFile(obj, true);
    else
      platform_->Move(obj, obj_in_downloads);
    LOG_IF(WARNING, !warning_sent)
        << "Processing files in " << downloads_in_myfiles;
    warning_sent = true;
  }

  if (!BindAndPush(downloads, downloads_in_myfiles))
    return false;

  return true;
}

bool MountHelper::MountAndPush(const base::FilePath& src,
                               const base::FilePath& dest,
                               const std::string& type,
                               const std::string& options) {
  if (!platform_->Mount(src, dest, type, kDefaultMountFlags, options)) {
    PLOG(ERROR) << "Mount failed: " << src.value() << " -> " << dest.value();
    return false;
  }

  stack_.Push(src, dest);
  return true;
}

bool MountHelper::BindAndPush(const FilePath& src, const FilePath& dest) {
  if (!platform_->Bind(src, dest)) {
    PLOG(ERROR) << "Bind mount failed: " << src.value() << " -> "
                << dest.value();
    return false;
  }

  stack_.Push(src, dest);
  return true;
}

bool MountHelper::MountDaemonStoreDirectories(
    const FilePath& root_home, const std::string& obfuscated_username) {
  // Iterate over all directories in /etc/daemon-store. This list is on rootfs,
  // so it's tamper-proof and nobody can sneak in additional directories that we
  // blindly mount. The actual mounts happen on /run/daemon-store, though.
  std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(FilePath(kEtcDaemonStoreBaseDir),
                                   false /* recursive */,
                                   base::FileEnumerator::DIRECTORIES));

  // /etc/daemon-store/<daemon-name>
  FilePath etc_daemon_store_path;
  while (!(etc_daemon_store_path = file_enumerator->Next()).empty()) {
    const FilePath& daemon_name = etc_daemon_store_path.BaseName();

    // /run/daemon-store/<daemon-name>
    FilePath run_daemon_store_path =
        FilePath(kRunDaemonStoreBaseDir).Append(daemon_name);
    if (!platform_->DirectoryExists(run_daemon_store_path)) {
      // The chromeos_startup script should make sure this exist.
      PLOG(ERROR) << "Daemon store directory does not exist: "
                  << run_daemon_store_path.value();
      return false;
    }

    // /home/.shadow/<user_hash>/mount/root/<daemon-name>
    const FilePath mount_source = root_home.Append(daemon_name);

    // /run/daemon-store/<daemon-name>/<user_hash>
    const FilePath mount_target =
        run_daemon_store_path.Append(obfuscated_username);

    if (!platform_->CreateDirectory(mount_source)) {
      LOG(ERROR) << "Failed to create directory " << mount_source.value();
      return false;
    }

    // The target directory's parent exists in the root mount namespace so the
    // directory itself can be created in the root mount namespace and it will
    // be visible in all namespaces.
    if (!platform_->CreateDirectory(mount_target)) {
      PLOG(ERROR) << "Failed to create directory " << mount_target.value();
      return false;
    }

    // Copy ownership from |etc_daemon_store_path| to |mount_source|. After the
    // bind operation, this guarantees that ownership for |mount_target| is the
    // same as for |etc_daemon_store_path| (usually
    // <daemon_user>:<daemon_group>), which is what the daemon intended.
    // Otherwise, it would end up being root-owned.
    base::stat_wrapper_t etc_daemon_path_stat =
        file_enumerator->GetInfo().stat();
    if (!platform_->SetOwnership(mount_source, etc_daemon_path_stat.st_uid,
                                 etc_daemon_path_stat.st_gid,
                                 false /*follow_links*/)) {
      LOG(ERROR) << "Failed to set ownership for " << mount_source.value();
      return false;
    }

    // Similarly, transfer directory permissions. Should usually be 0700, so
    // that only the daemon has full access.
    if (!platform_->SetPermissions(mount_source,
                                   etc_daemon_path_stat.st_mode)) {
      LOG(ERROR) << "Failed to set permissions for " << mount_source.value();
      return false;
    }

    // Assuming that |run_daemon_store_path| is a shared mount and the daemon
    // runs in a file system namespace with |run_daemon_store_path| mounted as
    // slave, this mount event propagates into the daemon.
    if (!BindAndPush(mount_source, mount_target))
      return false;
  }

  return true;
}

bool MountHelper::MountHomesAndDaemonStores(
    const std::string& username,
    const std::string& obfuscated_username,
    const FilePath& user_home,
    const FilePath& root_home) {
  // Mount /home/chronos/user.
  if (legacy_mount_ && !MountLegacyHome(user_home))
    return false;

  // Mount /home/chronos/u-<user_hash>
  const FilePath new_user_path = GetNewUserPath(username);
  if (!BindAndPush(user_home, new_user_path))
    return false;

  // Mount /home/user/<user_hash>.
  const FilePath user_multi_home = GetUserPath(username);
  if (!BindAndPush(user_home, user_multi_home))
    return false;

  // Mount /home/root/<user_hash>.
  const FilePath root_multi_home = GetRootPath(username);
  if (!BindAndPush(root_home, root_multi_home))
    return false;

  // Mount Downloads to MyFiles/Downloads in:
  //  - /home/chronos/u-<user_hash>
  //  - /home/user/<user_hash>
  if (!(BindMyFilesDownloads(new_user_path) &&
        BindMyFilesDownloads(user_multi_home))) {
    return false;
  }

  // Only bind mount /home/chronos/user/Downloads if it isn't mounted yet, in
  // multi-profile login it skips.
  if (legacy_mount_) {
    auto downloads_folder =
        FilePath(kDefaultHomeDir).Append(kMyFilesDir).Append(kDownloadsDir);

    if (platform_->IsDirectoryMounted(downloads_folder)) {
      LOG(INFO) << "Skipping binding to: " << downloads_folder.value();
    } else if (!BindMyFilesDownloads(FilePath(kDefaultHomeDir))) {
      return false;
    }
  }

  // Mount directories used by daemons to store per-user data.
  if (!MountDaemonStoreDirectories(root_home, obfuscated_username))
    return false;

  return true;
}

bool MountHelper::CreateTrackedSubdirectories(
    const std::string& obfuscated_username, const MountType& mount_type) const {
  brillo::ScopedUmask scoped_umask(kDefaultUmask);

  // Add the subdirectories if they do not exist.
  const FilePath dest_dir(
      mount_type == MountType::ECRYPTFS
          ? HomeDirs::GetEcryptfsUserVaultPath(shadow_root_,
                                               obfuscated_username)
          : HomeDirs::GetUserMountDirectory(shadow_root_, obfuscated_username));
  if (!platform_->DirectoryExists(dest_dir)) {
    LOG(ERROR) << "Can't create tracked subdirectories for a missing user.";
    return false;
  }

  const FilePath mount_dir(
      HomeDirs::GetUserMountDirectory(shadow_root_, obfuscated_username));

  // The call is allowed to partially fail if directory creation fails, but we
  // want to have as many of the specified tracked directories created as
  // possible.
  bool result = true;
  for (const auto& tracked_dir : GetTrackedSubdirectories()) {
    const FilePath tracked_dir_path = dest_dir.Append(tracked_dir);
    if (mount_type == MountType::ECRYPTFS) {
      const FilePath userside_dir = mount_dir.Append(tracked_dir);
      // If non-pass-through dir with the same name existed - delete it
      // to prevent duplication.
      if (platform_->DirectoryExists(userside_dir) &&
          !platform_->DirectoryExists(tracked_dir_path)) {
        platform_->DeleteFile(userside_dir, true);
      }
    }

    // Create pass-through directory.
    if (!platform_->DirectoryExists(tracked_dir_path)) {
      // Delete the existing file or symbolic link if any.
      platform_->DeleteFile(tracked_dir_path, false /* recursive */);
      VLOG(1) << "Creating pass-through directory " << tracked_dir_path.value();
      platform_->CreateDirectory(tracked_dir_path);
      if (!platform_->SetOwnership(tracked_dir_path, default_uid_, default_gid_,
                                   true /*follow_links*/)) {
        PLOG(ERROR) << "Couldn't change owner (" << default_uid_ << ":"
                    << default_gid_ << ") of tracked directory path: "
                    << tracked_dir_path.value();
        platform_->DeleteFile(tracked_dir_path, true);
        result = false;
        continue;
      }
    }
    if (mount_type == MountType::DIR_CRYPTO) {
      // Set xattr to make this directory trackable.
      std::string name = tracked_dir_path.BaseName().value();
      if (!platform_->SetExtendedFileAttribute(tracked_dir_path,
                                               kTrackedDirectoryNameAttribute,
                                               name.data(), name.length())) {
        PLOG(ERROR) << "Unable to set xattr on " << tracked_dir_path.value();
        result = false;
        continue;
      }
    }
  }
  return result;
}

bool MountHelper::PerformMount(const Options& mount_opts,
                               const std::string& username,
                               const std::string& fek_signature,
                               const std::string& fnek_signature,
                               bool is_pristine,
                               MountError* error) {
  const std::string obfuscated_username = SanitizeUserName(username);
  const FilePath vault_path =
      HomeDirs::GetEcryptfsUserVaultPath(shadow_root_, obfuscated_username);
  const FilePath mount_point =
      HomeDirs::GetUserMountDirectory(shadow_root_, obfuscated_username);

  std::string ecryptfs_options;
  bool should_mount_ecryptfs = mount_opts.type == MountType::ECRYPTFS ||
                               mount_opts.to_migrate_from_ecryptfs;
  if (should_mount_ecryptfs) {
    // Specify the ecryptfs options for mounting the user's cryptohome.
    ecryptfs_options = StringPrintf(
        "ecryptfs_cipher=aes"
        ",ecryptfs_key_bytes=%d"
        ",ecryptfs_fnek_sig=%s"
        ",ecryptfs_sig=%s"
        ",ecryptfs_unlink_sigs",
        kDefaultEcryptfsKeySize, fnek_signature.c_str(), fek_signature.c_str());

    // Create <vault_path>/user as a passthrough directory, move all the
    // (encrypted) contents of <vault_path> into <vault_path>/user, create
    // <vault_path>/root.
    MigrateToUserHome(vault_path);
  }

  if (mount_opts.type == MountType::DIR_CRYPTO) {
    // Create user & root directories.
    MigrateToUserHome(mount_point);
  }

  // Move the tracked subdirectories from <mount_point_>/user to <vault_path>
  // as passthrough directories.
  CreateTrackedSubdirectories(obfuscated_username, mount_opts.type);

  const FilePath user_home = GetMountedUserHomePath(obfuscated_username);
  const FilePath root_home = GetMountedRootHomePath(obfuscated_username);

  // b/115997660: Mount eCryptfs after creating the tracked subdirectories.
  if (should_mount_ecryptfs) {
    FilePath dest = mount_opts.to_migrate_from_ecryptfs
                        ? GetUserTemporaryMountDirectory(obfuscated_username)
                        : mount_point;
    if (!MountAndPush(vault_path, dest, "ecryptfs", ecryptfs_options)) {
      LOG(ERROR) << "eCryptfs mount failed";
      *error = MOUNT_ERROR_MOUNT_ECRYPTFS_FAILED;
      return false;
    }
  }

  if (is_pristine)
    CopySkeleton(user_home);

  if (!SetUpGroupAccess(FilePath(user_home))) {
    *error = MOUNT_ERROR_SETUP_GROUP_ACCESS_FAILED;
    return false;
  }

  // When migrating, it's better to avoid exposing the new ext4 crypto dir.
  // Also don't expose the home directory if a shadow-only mount was requested.
  if (!mount_opts.to_migrate_from_ecryptfs && !mount_opts.shadow_only &&
      !MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    *error = MOUNT_ERROR_MOUNT_HOMES_AND_DAEMON_STORES_FAILED;
    return false;
  }

  return true;
}

bool MountHelper::PrepareEphemeralDevice(
    const std::string& obfuscated_username) {
  // Underlying sparse file will be created in a temporary directory in RAM.
  const FilePath ephemeral_root(kEphemeralCryptohomeDir);

  // Determine ephemeral cryptohome size.
  struct statvfs vfs;
  if (!platform_->StatVFS(ephemeral_root, &vfs)) {
    PLOG(ERROR) << "Can't determine ephemeral cryptohome size";
    return false;
  }
  const int64_t sparse_size = static_cast<int64_t>(vfs.f_blocks * vfs.f_frsize);

  // Create underlying sparse file.
  const FilePath sparse_file = GetEphemeralSparseFile(obfuscated_username);
  if (!platform_->CreateDirectory(sparse_file.DirName())) {
    LOG(ERROR) << "Can't create directory for ephemeral sparse files";
    return false;
  }

  // Remember the file to clean up if an error happens during file creation.
  ephemeral_file_path_ = sparse_file;
  if (!platform_->CreateSparseFile(sparse_file, sparse_size)) {
    LOG(ERROR) << "Can't create ephemeral sparse file";
    return false;
  }

  // Format the sparse file as ext4.
  if (!platform_->FormatExt4(sparse_file, kDefaultExt4FormatOpts, 0)) {
    LOG(ERROR) << "Can't format ephemeral sparse file as ext4";
    return false;
  }

  // Create a loop device based on the sparse file.
  const FilePath loop_device = platform_->AttachLoop(sparse_file);
  if (loop_device.empty()) {
    LOG(ERROR) << "Can't create loop device";
    return false;
  }

  // Remember the loop device to clean up if an error happens.
  ephemeral_loop_device_ = loop_device;
  return true;
}

bool MountHelper::PerformEphemeralMount(const std::string& username) {
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);

  if (!PrepareEphemeralDevice(obfuscated_username)) {
    LOG(ERROR) << "Can't prepare ephemeral device";
    return false;
  }

  const FilePath mount_point =
      GetUserEphemeralMountDirectory(obfuscated_username);
  if (!platform_->CreateDirectory(mount_point)) {
    PLOG(ERROR) << "Directory creation failed for " << mount_point.value();
    return false;
  }
  if (!MountAndPush(ephemeral_loop_device_, mount_point, kEphemeralMountType,
                    kEphemeralMountOptions)) {
    LOG(ERROR) << "Can't mount ephemeral mount point";
    return false;
  }

  // Create user & root directories.
  MigrateToUserHome(mount_point);
  if (!EnsureUserMountPoints(username)) {
    return false;
  }

  const FilePath user_home =
      GetMountedEphemeralUserHomePath(obfuscated_username);
  const FilePath root_home =
      GetMountedEphemeralRootHomePath(obfuscated_username);

  if (!SetUpEphemeralCryptohome(user_home)) {
    return false;
  }

  if (!MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    return false;
  }

  return true;
}

bool MountHelper::TearDownEphemeralMount() {
  UnmountAll();
  return CleanUpEphemeral();
}

void MountHelper::TearDownNonEphemeralMount() {
  UnmountAll();
}

void MountHelper::UnmountAll() {
  FilePath src, dest;
  const FilePath ephemeral_mount_path =
      FilePath(kEphemeralCryptohomeDir).Append(kEphemeralMountDir);
  while (stack_.Pop(&src, &dest)) {
    ForceUnmount(src, dest);
    // Clean up destination directory for ephemeral loop device mounts.
    if (ephemeral_mount_path.IsParent(dest))
      platform_->DeleteFile(dest, true /* recursive */);
  }
}

bool MountHelper::CleanUpEphemeral() {
  bool success = true;
  if (!ephemeral_loop_device_.empty()) {
    if (!platform_->DetachLoop(ephemeral_loop_device_)) {
      PLOG(ERROR) << "Can't detach loop device '"
                  << ephemeral_loop_device_.value() << "'";
      success = false;
    }
    ephemeral_loop_device_.clear();
  }
  if (!ephemeral_file_path_.empty()) {
    if (!platform_->DeleteFile(ephemeral_file_path_, false /* recursive */)) {
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file '"
                  << ephemeral_file_path_.value() << "'";
      success = false;
    }
    ephemeral_file_path_.clear();
  }

  return success;
}

void MountHelper::ForceUnmount(const FilePath& src, const FilePath& dest) {
  // Try an immediate unmount.
  bool was_busy;
  if (!platform_->Unmount(dest, false, &was_busy)) {
    LOG(ERROR) << "Couldn't unmount '" << dest.value()
               << "' immediately, was_busy=" << std::boolalpha << was_busy;
    if (was_busy) {
      std::vector<ProcessInformation> processes;
      platform_->GetProcessesWithOpenFiles(dest, &processes);
      for (const auto& proc : processes) {
        LOG(ERROR) << "Process " << proc.get_process_id() << " had "
                   << proc.get_open_files().size()
                   << " open files.  Command line: " << proc.GetCommandLine();
        if (proc.get_cwd().length()) {
          LOG(ERROR) << "  (" << proc.get_process_id()
                     << ") CWD: " << proc.get_cwd();
        }
      }
    }
    // Failed to unmount immediately, do a lazy unmount.  If |was_busy| we also
    // want to sync before the unmount to help prevent data loss.
    if (was_busy)
      platform_->SyncDirectory(dest);
    platform_->LazyUnmount(dest);
    platform_->SyncDirectory(src);
  }
}

bool MountHelper::CanPerformEphemeralMount() const {
  return ephemeral_file_path_.empty() && ephemeral_loop_device_.empty();
}

bool MountHelper::MountPerformed() const {
  return stack_.size() > 0;
}

bool MountHelper::IsPathMounted(const base::FilePath& path) const {
  return stack_.ContainsDest(path);
}

std::vector<base::FilePath> MountHelper::MountedPaths() const {
  return stack_.MountDestinations();
}

}  // namespace cryptohome
