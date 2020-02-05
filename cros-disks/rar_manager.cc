// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {
namespace {

const char kExtension[] = ".rar";

}  // namespace

RarManager::RarManager(const std::string& mount_root,
                       Platform* const platform,
                       Metrics* const metrics,
                       brillo::ProcessReaper* const reaper)
    : MountManager(mount_root, platform, metrics, reaper) {}

RarManager::~RarManager() {
  UnmountAll();
}

bool RarManager::CanMount(const std::string& source_path) const {
  // Check for expected file extension.
  if (!base::EndsWith(source_path, kExtension,
                      base::CompareCase::INSENSITIVE_ASCII))
    return false;

  // The following paths can be mounted:
  //     /home/chronos/u-<user-id>/MyFiles/...<file>
  //     /media/archive/<dir>/...<file>
  //     /media/fuse/<dir>/...<file>
  //     /media/removable/<dir>/...<file>
  //     /run/arc/sdcard/write/emulated/0/<dir>/...<file>
  std::vector<std::string> parts;
  base::FilePath(source_path).GetComponents(&parts);

  if (parts.size() < 2 || parts[0] != "/")
    return false;

  if (parts[1] == "home")
    return parts.size() > 5 && parts[2] == "chronos" &&
           base::StartsWith(parts[3], "u-", base::CompareCase::SENSITIVE) &&
           brillo::cryptohome::home::IsSanitizedUserName(parts[3].substr(2)) &&
           parts[4] == "MyFiles";

  if (parts[1] == "media")
    return parts.size() > 4 && (parts[2] == "archive" || parts[2] == "fuse" ||
                                parts[2] == "removable");

  if (parts[1] == "run")
    return parts.size() > 8 && parts[2] == "arc" && parts[3] == "sdcard" &&
           parts[4] == "write" && parts[5] == "emulated" && parts[6] == "0";

  return false;
}

std::string RarManager::SuggestMountPath(const std::string& source_path) const {
  const base::FilePath base_name = base::FilePath(source_path).BaseName();
  return mount_root().Append(base_name).value();
}

std::unique_ptr<MountPoint> RarManager::DoMount(
    const std::string& source_path,
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& /*options*/,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(applied_options);
  DCHECK(error);

  metrics()->RecordArchiveType("rar");

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
  options.Initialize({MountOptions::kOptionReadOnly}, true,
                     base::IntToString(files_uid),
                     base::IntToString(files_gid));

  *applied_options = options;

  // Run rar2fs.
  // TODO(crbug.com/996549): Create a seccomp policy.
  // TODO(crbug.com/996549): Handle archives in Play Files.
  FUSEMounter mounter(
      "rarfs", options, platform(), process_reaper(), "/usr/bin/rar2fs",
      "fuse-rar2fs", "" /* seccomp_policy */, GetBindPaths(source_path),
      false /* permit_network_access */, FUSEHelper::kFilesGroup);

  return mounter.Mount(source_path, mount_path, {}, error);
}

std::vector<FUSEMounter::BindPath> RarManager::GetBindPaths(
    const base::StringPiece s) const {
  std::vector<FUSEMounter::BindPath> paths = {{std::string(s)}};
  // TODO(crbug.com/221124): Handle multipart archives.
  return paths;
}

}  // namespace cros_disks
