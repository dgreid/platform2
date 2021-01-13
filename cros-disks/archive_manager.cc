// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>

#include "cros-disks/archive_mounter.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/rar_mounter.h"
#include "cros-disks/user.h"

namespace cros_disks {

ArchiveManager::ArchiveManager(const std::string& mount_root,
                               Platform* platform,
                               Metrics* metrics,
                               brillo::ProcessReaper* process_reaper)
    : MountManager(mount_root, platform, metrics, process_reaper) {}

ArchiveManager::~ArchiveManager() = default;

bool ArchiveManager::Initialize() {
  if (!MountManager::Initialize())
    return false;

  {
    SandboxedExecutable executable = {
        base::FilePath("/usr/bin/fuse-zip"),
        base::FilePath("/usr/share/policy/fuse-zip-seccomp.policy")};

    auto sandbox_factory =
        CreateSandboxFactory(std::move(executable), "fuse-zip");
    std::vector<int> password_needed_codes = {
        23,   // ZIP_ER_BASE + ZIP_ER_ZLIB
        36,   // ZIP_ER_BASE + ZIP_ER_NOPASSWD
        37};  // ZIP_ER_BASE + ZIP_ER_WRONGPASSWD

    mounters_.push_back(std::make_unique<ArchiveMounter>(
        platform(), process_reaper(), "zip", metrics(), "FuseZip",
        std::move(password_needed_codes), std::move(sandbox_factory)));
  }

  {
    SandboxedExecutable executable = {
        base::FilePath("/usr/bin/rar2fs"),
        base::FilePath("/usr/share/policy/rar2fs-seccomp.policy")};

    auto sandbox_factory =
        CreateSandboxFactory(std::move(executable), "fuse-rar2fs");

    mounters_.push_back(std::make_unique<RarMounter>(
        platform(), process_reaper(), metrics(), std::move(sandbox_factory)));
  }

  return true;
}

bool ArchiveManager::ResolvePath(const std::string& path,
                                 std::string* real_path) {
  std::unique_ptr<brillo::ScopedMountNamespace> mount_ns;
  if (!platform()->PathExists(path)) {
    // Try to locate the file in Chrome's mount namespace.
    mount_ns = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(ArchiveMounter::kChromeNamespace));
    if (!mount_ns) {
      PLOG(ERROR) << "Could not look for archive " << quote(path)
                  << " in the Chrome's namespace";
      return false;
    }
  }
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

bool ArchiveManager::CanMount(const std::string& source_path) const {
  if (IsInAllowedFolder(source_path)) {
    base::FilePath name;
    for (const auto& m : mounters_) {
      if (m->CanMount(source_path, {}, &name)) {
        return true;
      }
    }
  }
  return false;
}

std::unique_ptr<MountPoint> ArchiveManager::DoMount(
    const std::string& source_path,
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    bool* mounted_as_read_only,
    MountErrorType* error) {
  // Here source_path is already resolved and free from symlinks and '..' by
  // the base class.
  if (!IsInAllowedFolder(source_path)) {
    LOG(ERROR) << "Source path " << quote(source_path) << " is not allowed";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }
  base::FilePath name;
  for (const auto& m : mounters_) {
    if (m->CanMount(source_path, {}, &name)) {
      *mounted_as_read_only = true;
      return m->Mount(source_path, mount_path, options, error);
    }
  }
  LOG(ERROR) << "Cannot find mounter for archive " << quote(source_path);
  *error = MOUNT_ERROR_UNKNOWN_FILESYSTEM;
  return nullptr;
}

std::unique_ptr<FUSESandboxedProcessFactory>
ArchiveManager::CreateSandboxFactory(SandboxedExecutable executable,
                                     const std::string& user_name) const {
  // To access Play Files.
  std::vector<gid_t> groups;
  gid_t gid;
  if (platform()->GetGroupId("android-everybody", &gid))
    groups.push_back(gid);

  OwnerUser run_as;
  if (!platform()->GetUserAndGroupId(user_name, &run_as.uid, &run_as.gid)) {
    PLOG(ERROR) << "Cannot resolve required user " << quote(user_name);
    return nullptr;
  }
  // Archivers need to run in chronos-access group to be able to access
  // user's files.
  run_as.gid = kChronosAccessGID;

  return std::make_unique<FUSESandboxedProcessFactory>(
      platform(), std::move(executable), std::move(run_as),
      /* has_network_access= */ false, std::move(groups));
}

}  // namespace cros_disks
