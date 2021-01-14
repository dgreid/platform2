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
    bool* mounted_as_read_only,
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
    LOG(ERROR) << "Cannot find FUSE module for " << redact(source)
               << " of type " << quote(fuse_type);
    *error = MOUNT_ERROR_UNKNOWN_FILESYSTEM;
    return nullptr;
  }

  *mounted_as_read_only = IsReadOnlyMount(options);
  auto mountpoint = selected_helper->Mount(source, mount_path, options, error);
  LOG_IF(ERROR, *error != MOUNT_ERROR_NONE)
      << "Cannot mount " << redact(source) << " of type " << quote(fuse_type)
      << ": " << *error;

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
