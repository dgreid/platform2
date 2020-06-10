// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/zip_manager.h"

#include <algorithm>
#include <memory>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

ZipManager::~ZipManager() {
  UnmountAll();
}

bool ZipManager::CanMount(const std::string& source_path) const {
  // Check for expected file extension.
  return base::EndsWith(source_path, ".zip",
                        base::CompareCase::INSENSITIVE_ASCII) &&
         IsInAllowedFolder(source_path);
}

std::unique_ptr<MountPoint> ZipManager::DoMount(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& original_options,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(applied_options);
  DCHECK(error);

  // Get appropriate UID and GID.
  uid_t files_uid;
  gid_t files_gid;
  if (!platform()->GetUserAndGroupId(FUSEHelper::kFilesUser, &files_uid,
                                     nullptr) ||
      !platform()->GetGroupId(FUSEHelper::kFilesGroup, &files_gid)) {
    *error = MOUNT_ERROR_INTERNAL;
    return nullptr;
  }

  // Prepare FUSE mount options.
  MountOptions options;
  // FUSE umask option in octal 0222 == r-x r-x r-x
  options.WhitelistOptionPrefix("umask=");
  options.Initialize({"umask=0222", MountOptions::kOptionReadOnly}, true,
                     base::NumberToString(files_uid),
                     base::NumberToString(files_gid));

  *applied_options = options;

  // Run fuse-zip.
  FUSEMounter mounter(
      "zipfs", options, platform(), process_reaper(), "/usr/bin/fuse-zip",
      "fuse-zip", "/usr/share/policy/fuse-zip-seccomp.policy", {{source_path}},
      false /* permit_network_access */, FUSEHelper::kFilesGroup);

  // To access Play Files.
  if (!mounter.AddGroup("android-everybody"))
    LOG(INFO) << "Group 'android-everybody' does not exist";

  return mounter.Mount(source_path, mount_path, {}, error);
}

}  // namespace cros_disks
