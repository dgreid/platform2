// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implements cros-disks::MountManager. See mount-manager.h for details.

#include "cros-disks/mount_manager.h"

#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

// Permissions to set on the mount root directory (u+rwx,og+rx).
const mode_t kMountRootDirectoryPermissions =
    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
// Prefix of the mount label option.
const char kMountOptionMountLabelPrefix[] = "mountlabel=";
// Literal for mount option: "remount".
const char kMountOptionRemount[] = "remount";
// Maximum number of trials on creating a mount directory using
// Platform::CreateOrReuseEmptyDirectoryWithFallback().
// A value of 100 seems reasonable and enough to handle directory name
// collisions under common scenarios.
const unsigned kMaxNumMountTrials = 100;

}  // namespace

MountManager::MountManager(const std::string& mount_root,
                           Platform* platform,
                           Metrics* metrics,
                           brillo::ProcessReaper* process_reaper)
    : mount_root_(base::FilePath(mount_root)),
      platform_(platform),
      metrics_(metrics),
      process_reaper_(process_reaper) {
  CHECK(!mount_root_.empty()) << "Invalid mount root directory";
  CHECK(mount_root_.IsAbsolute()) << "Mount root not absolute path";
  CHECK(platform_) << "Invalid platform object";
  CHECK(metrics_) << "Invalid metrics object";
}

MountManager::~MountManager() {
  // UnmountAll() should be called from a derived class instead of this base
  // class as UnmountAll() calls MountPoint::Unmount() which may call back into
  // a derived class.
}

bool MountManager::Initialize() {
  return platform_->CreateDirectory(mount_root_.value()) &&
         platform_->SetOwnership(mount_root_.value(), getuid(), getgid()) &&
         platform_->SetPermissions(mount_root_.value(),
                                   kMountRootDirectoryPermissions);
}

bool MountManager::StartSession() {
  return true;
}

bool MountManager::StopSession() {
  return UnmountAll();
}

MountErrorType MountManager::Mount(const std::string& source_path,
                                   const std::string& filesystem_type,
                                   const std::vector<std::string>& options,
                                   std::string* mount_path) {
  // Source is not necessary a path, but if it is let's resolve it to
  // some real underlying object.
  std::string real_path;
  if (Uri::IsUri(source_path) || !ResolvePath(source_path, &real_path)) {
    real_path = source_path;
  }

  if (real_path.empty()) {
    LOG(ERROR) << "Failed to mount an invalid path";
    return MOUNT_ERROR_INVALID_ARGUMENT;
  }
  if (!mount_path) {
    LOG(ERROR) << "Invalid mount path argument";
    return MOUNT_ERROR_INVALID_ARGUMENT;
  }

  if (find(options.begin(), options.end(), kMountOptionRemount) ==
      options.end()) {
    return MountNewSource(real_path, filesystem_type, options, mount_path);
  } else {
    return Remount(real_path, filesystem_type, options, mount_path);
  }
}

MountErrorType MountManager::Remount(const std::string& source_path,
                                     const std::string& filesystem_type,
                                     const std::vector<std::string>& options,
                                     std::string* mount_path) {
  if (!GetMountPathFromCache(source_path, mount_path)) {
    LOG(WARNING) << "Path " << quote(source_path) << " is not mounted yet";
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }

  std::vector<std::string> updated_options = options;
  std::string mount_label;
  ExtractMountLabelFromOptions(&updated_options, &mount_label);

  // Perform the underlying mount operation.
  MountErrorType error_type = MOUNT_ERROR_UNKNOWN;
  bool mounted_as_read_only = false;
  std::unique_ptr<MountPoint> mount_point =
      DoMount(source_path, filesystem_type, updated_options,
              base::FilePath(*mount_path), &mounted_as_read_only, &error_type);
  if (error_type != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Cannot remount path " << quote(source_path)
               << "': " << error_type;
    return error_type;
  }

  DCHECK(mount_point);
  LOG(INFO) << "Path " << quote(source_path) << " on " << quote(*mount_path)
            << " is remounted";
  AddOrUpdateMountStateCache(source_path, std::move(mount_point),
                             mounted_as_read_only);

  return error_type;
}

bool MountManager::ResolvePath(const std::string& path,
                               std::string* real_path) {
  return platform_->GetRealPath(path, real_path);
}

MountErrorType MountManager::MountNewSource(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& options,
    std::string* mount_path) {
  std::string actual_mount_path;
  if (GetMountPathFromCache(source_path, &actual_mount_path)) {
    LOG(WARNING) << "Path " << quote(source_path) << " is already mounted to "
                 << quote(actual_mount_path);
    // TODO(benchan): Should probably compare filesystem type and mount options
    //                with those used in previous mount.
    if (mount_path->empty() || *mount_path == actual_mount_path) {
      *mount_path = actual_mount_path;
      return GetMountErrorOfReservedMountPath(actual_mount_path);
    } else {
      return MOUNT_ERROR_PATH_ALREADY_MOUNTED;
    }
  }

  std::vector<std::string> updated_options = options;
  std::string mount_label;
  ExtractMountLabelFromOptions(&updated_options, &mount_label);

  // Create a directory and set up its ownership/permissions for mounting
  // the source path. If an error occurs, ShouldReserveMountPathOnError()
  // is not called to reserve the mount path as a reserved mount path still
  // requires a proper mount directory.
  if (mount_path->empty()) {
    actual_mount_path = SuggestMountPath(source_path);
    if (!mount_label.empty()) {
      // Replace the basename(|actual_mount_path|) with |mount_label|.
      actual_mount_path = base::FilePath(actual_mount_path)
                              .DirName()
                              .Append(mount_label)
                              .value();
    }
  } else {
    actual_mount_path = *mount_path;
  }

  if (!IsValidMountPath(base::FilePath(actual_mount_path))) {
    LOG(ERROR) << "Mount path " << quote(actual_mount_path) << " is invalid";
    return MOUNT_ERROR_INVALID_PATH;
  }

  bool mount_path_created;
  if (mount_path->empty()) {
    mount_path_created = platform_->CreateOrReuseEmptyDirectoryWithFallback(
        &actual_mount_path, kMaxNumMountTrials, GetReservedMountPaths());
  } else {
    mount_path_created =
        !IsMountPathReserved(actual_mount_path) &&
        platform_->CreateOrReuseEmptyDirectory(actual_mount_path);
  }
  if (!mount_path_created) {
    LOG(ERROR) << "Cannot create directory " << quote(actual_mount_path)
               << " to mount " << quote(source_path);
    return MOUNT_ERROR_DIRECTORY_CREATION_FAILED;
  }

  // Perform the underlying mount operation. If an error occurs,
  // ShouldReserveMountPathOnError() is called to check if the mount path
  // should be reserved.
  bool mounted_as_read_only = false;
  MountErrorType error_type = MOUNT_ERROR_UNKNOWN;
  std::unique_ptr<MountPoint> mount_point = DoMount(
      source_path, filesystem_type, updated_options,
      base::FilePath(actual_mount_path), &mounted_as_read_only, &error_type);
  if (error_type == MOUNT_ERROR_NONE) {
    LOG(INFO) << "Path " << quote(source_path) << " is mounted to "
              << quote(actual_mount_path);
    DCHECK(mount_point);
  } else if (ShouldReserveMountPathOnError(error_type)) {
    LOG(INFO) << "Reserving mount path " << quote(actual_mount_path) << " for "
              << quote(source_path);
    DCHECK(!mount_point);
    ReserveMountPath(actual_mount_path, error_type);
    mount_point = MountPoint::CreateLeaking(base::FilePath(actual_mount_path));
  } else {
    LOG(ERROR) << "Cannot mount " << redact(source_path) << " of type "
               << quote(filesystem_type) << ": " << error_type;
    platform_->RemoveEmptyDirectory(actual_mount_path);
    return error_type;
  }

  AddOrUpdateMountStateCache(source_path, std::move(mount_point),
                             mounted_as_read_only);
  *mount_path = actual_mount_path;
  return error_type;
}

MountErrorType MountManager::Unmount(const std::string& path) {
  // Determine whether the path is a source path or a mount path.
  std::string mount_path;
  // Is path a source path?
  if (!GetMountPathFromCache(path, &mount_path)) {
    // Not a source path. Is path a mount path?
    if (!IsMountPathInCache(path)) {
      // Not a mount path either.
      return MOUNT_ERROR_PATH_NOT_MOUNTED;
    }

    mount_path = path;
  }

  MountErrorType error_type = MOUNT_ERROR_NONE;
  if (IsMountPathReserved(mount_path)) {
    LOG(INFO) << "Removing mount path '" << mount_path
              << "' from the reserved list";
    UnreserveMountPath(mount_path);
  } else {
    MountPoint* mount_point = nullptr;
    for (const auto& entry : mount_states_) {
      if (mount_path == entry.second.mount_point->path().value()) {
        mount_point = entry.second.mount_point.get();
        break;
      }
    }
    DCHECK(mount_point);
    error_type = mount_point->Unmount();

    switch (error_type) {
      case MOUNT_ERROR_NONE:
        LOG(INFO) << "Unmounted " << quote(mount_path);
        break;

      case MOUNT_ERROR_PATH_NOT_MOUNTED:
        LOG(WARNING) << "Not mounted " << quote(mount_path);
        break;

      default:
        LOG(ERROR) << "Cannot unmount " << quote(mount_path) << ": "
                   << error_type;
        return error_type;
    }
  }

  RemoveMountPathFromCache(mount_path);
  platform_->RemoveEmptyDirectory(mount_path);
  return error_type;
}

bool MountManager::UnmountAll() {
  bool all_umounted = true;

  // Enumerate all the source paths and then unmount, as calling Unmount()
  // modifies the cache.
  std::vector<std::string> source_paths;
  source_paths.reserve(mount_states_.size());
  for (const auto& entry : mount_states_) {
    source_paths.push_back(entry.second.mount_point->path().value());
  }

  for (const auto& source_path : source_paths) {
    if (Unmount(source_path) != MOUNT_ERROR_NONE) {
      all_umounted = false;
    }
  }

  return all_umounted;
}

void MountManager::AddOrUpdateMountStateCache(
    const std::string& source_path,
    std::unique_ptr<MountPoint> mount_point,
    const bool is_read_only) {
  DCHECK(mount_point);

  MountState& mount_state = mount_states_[source_path];
  if (mount_state.mount_point) {
    LOG_IF(ERROR, mount_state.mount_point->path() != mount_point->path())
        << "Replacing source path " << quote(source_path)
        << " with new mount point " << quote(mount_point->path())
        << " != existing mount point "
        << quote(mount_state.mount_point->path());
    // This is a remount, so release the existing mount so that it doesn't
    // become unmounted on destruction.
    mount_state.mount_point->Release();
  }

  mount_state.mount_point = std::move(mount_point);
  mount_state.is_read_only = is_read_only;
}

bool MountManager::GetMountPathFromCache(const std::string& source_path,
                                         std::string* const mount_path) const {
  DCHECK(mount_path);

  const auto it = mount_states_.find(source_path);
  if (it == mount_states_.end())
    return false;

  *mount_path = it->second.mount_point->path().value();
  return true;
}

bool MountManager::IsMountPathInCache(const std::string& mount_path) const {
  for (const auto& entry : mount_states_) {
    if (entry.second.mount_point->path().value() == mount_path)
      return true;
  }
  return false;
}

bool MountManager::RemoveMountPathFromCache(const std::string& mount_path) {
  for (auto it = mount_states_.begin(); it != mount_states_.end(); ++it) {
    if (it->second.mount_point->path().value() == mount_path) {
      mount_states_.erase(it);
      return true;
    }
  }
  return false;
}

bool MountManager::IsMountPathReserved(const std::string& mount_path) const {
  return base::Contains(reserved_mount_paths_, mount_path);
}

MountErrorType MountManager::GetMountErrorOfReservedMountPath(
    const std::string& mount_path) const {
  const auto it = reserved_mount_paths_.find(mount_path);
  return it != reserved_mount_paths_.end() ? it->second : MOUNT_ERROR_NONE;
}

std::set<std::string> MountManager::GetReservedMountPaths() const {
  std::set<std::string> reserved_paths;
  for (const auto& entry : reserved_mount_paths_) {
    reserved_paths.insert(entry.first);
  }
  return reserved_paths;
}

void MountManager::ReserveMountPath(const std::string& mount_path,
                                    MountErrorType error_type) {
  reserved_mount_paths_.insert({mount_path, error_type});
}

void MountManager::UnreserveMountPath(const std::string& mount_path) {
  reserved_mount_paths_.erase(mount_path);
}

std::vector<MountEntry> MountManager::GetMountEntries() const {
  std::vector<MountEntry> mount_entries;
  mount_entries.reserve(mount_states_.size());
  for (const auto& entry : mount_states_) {
    const std::string& source_path = entry.first;
    const MountState& mount_state = entry.second;
    const std::string& mount_path = mount_state.mount_point->path().value();
    mount_entries.push_back({GetMountErrorOfReservedMountPath(mount_path),
                             source_path, GetMountSourceType(), mount_path,
                             mount_state.is_read_only});
  }
  return mount_entries;
}

base::Optional<MountEntry> MountManager::GetMountEntryForTest(
    const std::string& source_path) const {
  const auto it = mount_states_.find(source_path);
  if (it == mount_states_.end())
    return {};

  return MountEntry(MOUNT_ERROR_NONE, source_path, GetMountSourceType(),
                    it->second.mount_point->path().value(),
                    it->second.is_read_only);
}

bool MountManager::ExtractMountLabelFromOptions(
    std::vector<std::string>* const options,
    std::string* const mount_label) const {
  DCHECK(options);
  DCHECK(mount_label);

  mount_label->clear();
  bool found_mount_label = false;

  const base::StringPiece prefix = kMountOptionMountLabelPrefix;

  for (auto it = options->begin(); it != options->end();) {
    if (!base::StartsWith(*it, prefix, base::CompareCase::INSENSITIVE_ASCII)) {
      ++it;
      continue;
    }

    found_mount_label = true;
    *mount_label = it->substr(prefix.size());
    it = options->erase(it);
  }

  return found_mount_label;
}

bool MountManager::ShouldReserveMountPathOnError(
    MountErrorType error_type) const {
  return false;
}

bool MountManager::IsPathImmediateChildOfParent(
    const base::FilePath& path, const base::FilePath& parent) const {
  std::vector<std::string> path_components, parent_components;
  path.StripTrailingSeparators().GetComponents(&path_components);
  parent.StripTrailingSeparators().GetComponents(&parent_components);
  if (path_components.size() != parent_components.size() + 1)
    return false;

  if (path_components.back() == base::FilePath::kCurrentDirectory ||
      path_components.back() == base::FilePath::kParentDirectory) {
    return false;
  }

  return std::equal(parent_components.begin(), parent_components.end(),
                    path_components.begin());
}

bool MountManager::IsValidMountPath(const base::FilePath& mount_path) const {
  return IsPathImmediateChildOfParent(mount_path, mount_root_);
}

}  // namespace cros_disks
