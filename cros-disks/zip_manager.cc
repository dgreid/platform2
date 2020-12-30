// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/zip_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "cros-disks/archive_mounter.h"
#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

namespace {

std::unique_ptr<ArchiveMounter> CreateZipMounter(
    Platform* platform,
    Metrics* metrics,
    brillo::ProcessReaper* process_reaper,
    std::vector<gid_t> supplementary_groups) {
  OwnerUser run_as;
  PCHECK(platform->GetUserAndGroupId("fuse-zip", &run_as.uid, &run_as.gid))
      << "Cannot resolve required user fuse-zip";

  const SandboxedExecutable executable = {
      base::FilePath("/usr/bin/fuse-zip"),
      base::FilePath("/usr/share/policy/fuse-zip-seccomp.policy")};

  auto sandbox_factory = std::make_unique<FUSESandboxedProcessFactory>(
      platform, std::move(executable), std::move(run_as),
      /* has_network_access= */ false, std::move(supplementary_groups));

  std::vector<int> password_needed_codes = {
      23,   // ZIP_ER_BASE + ZIP_ER_ZLIB
      36,   // ZIP_ER_BASE + ZIP_ER_NOPASSWD
      37};  // ZIP_ER_BASE + ZIP_ER_WRONGPASSWD

  return std::make_unique<ArchiveMounter>(
      platform, process_reaper, "zip", metrics, "FuseZip",
      std::move(password_needed_codes), std::move(sandbox_factory));
}

}  // namespace

ZipManager::ZipManager(const std::string& mount_root,
                       Platform* platform,
                       Metrics* metrics,
                       brillo::ProcessReaper* process_reaper)
    : ArchiveManager(mount_root, platform, metrics, process_reaper),
      mounter_(CreateZipMounter(
          platform, metrics, process_reaper, GetSupplementaryGroups())) {}

ZipManager::~ZipManager() {
  UnmountAll();
}

bool ZipManager::CanMount(const std::string& source_path) const {
  base::FilePath name;
  return IsInAllowedFolder(source_path) &&
         mounter_->CanMount(source_path, {}, &name);
}

std::unique_ptr<MountPoint> ZipManager::DoMount(
    const std::string& source_path,
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    bool* mounted_as_read_only,
    MountErrorType* const error) {
  DCHECK(error);
  // MountManager resolves source path to real path before calling DoMount,
  // so no symlinks or '..' will be here.
  if (!IsInAllowedFolder(source_path)) {
    LOG(ERROR) << "Source path " << quote(source_path) << " is not allowed";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }
  *mounted_as_read_only = true;
  return mounter_->Mount(source_path, mount_path, options, error);
}

}  // namespace cros_disks
