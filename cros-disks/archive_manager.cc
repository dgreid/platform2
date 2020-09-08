// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>

#include "cros-disks/fuse_helper.h"
#include "cros-disks/platform.h"

namespace cros_disks {

bool ArchiveManager::ResolvePath(const std::string& path,
                                 std::string* real_path) {
  const MountNamespace mount_namespace = GetMountNamespaceFor(path);
  return platform()->GetRealPath(path, real_path);
}

bool ArchiveManager::IsInAllowedFolder(const std::string& source_path) {
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

std::string ArchiveManager::SuggestMountPath(
    const std::string& source_path) const {
  // Use the archive name to name the mount directory.
  base::FilePath base_name = base::FilePath(source_path).BaseName();
  return mount_root().Append(base_name).value();
}

std::vector<gid_t> ArchiveManager::GetSupplementaryGroups() const {
  std::vector<gid_t> groups;

  // To access Play Files.
  gid_t gid;
  if (platform()->GetGroupId("android-everybody", &gid))
    groups.push_back(gid);

  return groups;
}

MountErrorType ArchiveManager::GetMountOptions(
    MountOptions* const options) const {
  DCHECK(options);

  uid_t uid;
  gid_t gid;
  if (!platform()->GetUserAndGroupId(FUSEHelper::kFilesUser, &uid, nullptr) ||
      !platform()->GetGroupId(FUSEHelper::kFilesGroup, &gid))
    return MOUNT_ERROR_INTERNAL;

  options->SetReadOnlyOption();
  options->EnforceOption("umask=0222");
  options->EnforceOption(MountOptions::kOptionNoSymFollow);
  options->Initialize({}, true, base::NumberToString(uid),
                      base::NumberToString(gid));

  return MOUNT_ERROR_NONE;
}

ArchiveManager::MountNamespace ArchiveManager::GetMountNamespaceFor(
    const std::string& path) {
  const char* const chrome_namespace = "/run/namespaces/mnt_chrome";
  // Try to enter Chrome's mount namespace.
  MountNamespace result{.guard = brillo::ScopedMountNamespace::CreateFromPath(
                            base::FilePath(chrome_namespace))};
  // Check if the given path exists in Chrome's mount namespace.
  if (result.guard && base::PathExists(base::FilePath(path))) {
    result.name = chrome_namespace;
  } else {
    // The path doesn't exist in Chrome's mount namespace. Exit the namespace.
    result.guard.reset();
  }
  return result;
}

}  // namespace cros_disks
