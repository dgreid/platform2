// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/zip_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
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
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(applied_options);
  DCHECK(error);

  metrics()->RecordArchiveType("zip");

  FUSEMounterLegacy::Params params{
      .bind_paths = {{source_path}},
      .filesystem_type = "zipfs",
      .metrics = metrics(),
      .metrics_name = "FuseZip",
      .mount_namespace = GetMountNamespaceFor(source_path).name,
      .mount_program = "/usr/bin/fuse-zip",
      .mount_user = "fuse-zip",
      .password_needed_codes = {23,   // ZIP_ER_BASE + ZIP_ER_ZLIB
                                36,   // ZIP_ER_BASE + ZIP_ER_NOPASSWD
                                37},  // ZIP_ER_BASE + ZIP_ER_WRONGPASSWD
      .platform = platform(),
      .process_reaper = process_reaper(),
      .seccomp_policy = "/usr/share/policy/fuse-zip-seccomp.policy",
      .supplementary_groups = GetSupplementaryGroups(),
  };

  // Prepare FUSE mount options.
  *error = GetMountOptions(&params.mount_options);
  if (*error != MOUNT_ERROR_NONE)
    return nullptr;

  *applied_options = params.mount_options;

  // Run fuse-zip.
  const FUSEMounterLegacy mounter(std::move(params));
  return mounter.Mount(source_path, mount_path, options, error);
}

}  // namespace cros_disks
