// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>
#include <brillo/scoped_mount_namespace.h>

#include "cros-disks/platform.h"

namespace cros_disks {

const char* const ArchiveManager::kChromeMountNamespacePath =
    "/run/namespaces/mnt_chrome";

bool ArchiveManager::ResolvePath(const std::string& path,
                                 std::string* real_path) {
  auto guard = brillo::ScopedMountNamespace::CreateFromPath(
      base::FilePath(kChromeMountNamespacePath));

  // If the path doesn't exist in Chrome's mount namespace, exit the namespace,
  // so that GetRealPath() below gets executed in cros-disks's mount namespace.
  if (!base::PathExists(base::FilePath(path)))
    guard.reset();

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

}  // namespace cros_disks
