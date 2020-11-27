// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fuse_mount_manager.h"

#include <sys/mount.h>

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/process/process_reaper.h>

#include "cros-disks/drivefs_helper.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/smbfs_helper.h"
#include "cros-disks/sshfs_helper.h"
#include "cros-disks/uri.h"

namespace cros_disks {

FUSEMountManager::FUSEMountManager(const std::string& mount_root,
                                   const std::string& working_dirs_root,
                                   Platform* platform,
                                   Metrics* metrics,
                                   brillo::ProcessReaper* process_reaper)
    : MountManager(mount_root, platform, metrics, process_reaper),
      working_dirs_root_(working_dirs_root) {}

FUSEMountManager::~FUSEMountManager() {
  UnmountAll();
}

bool FUSEMountManager::Initialize() {
  if (!MountManager::Initialize())
    return false;

  if (!platform()->DirectoryExists(working_dirs_root_) &&
      !platform()->CreateDirectory(working_dirs_root_)) {
    LOG(ERROR) << "Can't create writable FUSE directory";
    return false;
  }
  if (!platform()->SetOwnership(working_dirs_root_, getuid(), getgid()) ||
      !platform()->SetPermissions(working_dirs_root_, 0755)) {
    LOG(ERROR) << "Can't set up writable FUSE directory";
    return false;
  }

  // Register specific FUSE mount helpers here.
  RegisterHelper(std::make_unique<DrivefsHelper>(platform(), process_reaper()));
  RegisterHelper(std::make_unique<SshfsHelper>(
      platform(), process_reaper(), base::FilePath(working_dirs_root_)));
  RegisterHelper(std::make_unique<SmbfsHelper>(platform(), process_reaper()));

  return true;
}

std::unique_ptr<MountPoint> FUSEMountManager::DoMount(
    const std::string& source,
    const std::string& fuse_type,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* applied_options,
    MountErrorType* error) {
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  Uri uri = Uri::Parse(source);
  CHECK(uri.valid()) << "Source " << quote(source) << " is not a URI";

  base::FilePath dir_name;
  const Mounter* selected_helper = nullptr;
  for (const auto& helper : helpers_) {
    if (helper->CanMount(source, options, &dir_name)) {
      selected_helper = helper.get();
      break;
    }
  }

  if (!selected_helper) {
    LOG(ERROR) << "Cannot find suitable FUSE module for type "
               << quote(fuse_type) << " and source " << quote(source);
    *error = MOUNT_ERROR_UNKNOWN_FILESYSTEM;
    return nullptr;
  }

  // Make a temporary dir where the helper may keep stuff needed by the mounter
  // process.
  std::string path;
  if (!platform()->CreateTemporaryDirInDir(working_dirs_root_, ".", &path) ||
      !platform()->SetPermissions(path, 0755)) {
    LOG(ERROR) << "Cannot create working directory for FUSE module mounting "
               << quote(source);
    *error = MOUNT_ERROR_DIRECTORY_CREATION_FAILED;
    return nullptr;
  }

  auto mountpoint =
      FUSEHelper::MountWithDir(*selected_helper, base::FilePath(path), source,
                               mount_path, options, error);
  LOG_IF(ERROR, *error != MOUNT_ERROR_NONE)
      << "Mounting failed for source " << quote(source) << ": " << *error;
  return mountpoint;
}

bool FUSEMountManager::CanMount(const std::string& source) const {
  base::FilePath dir;
  for (const auto& helper : helpers_) {
    if (helper->CanMount(source, {}, &dir))
      return true;
  }
  return false;
}

std::string FUSEMountManager::SuggestMountPath(
    const std::string& source) const {
  Uri uri = Uri::Parse(source);
  if (!uri.valid()) {
    return "";
  }

  base::FilePath dir;
  for (const auto& helper : helpers_) {
    if (helper->CanMount(source, {}, &dir))
      return mount_root().Append(dir).value();
  }
  base::FilePath base_name = base::FilePath(source).BaseName();
  return mount_root().Append(base_name).value();
}

void FUSEMountManager::RegisterHelper(std::unique_ptr<Mounter> helper) {
  helpers_.push_back(std::move(helper));
}

}  // namespace cros_disks
