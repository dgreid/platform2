// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <algorithm>
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
  // FUSE umask option in octal 0222 == r-x r-x r-x
  options.WhitelistOptionPrefix("umask=");
  options.Initialize({"umask=0222", MountOptions::kOptionReadOnly}, true,
                     base::IntToString(files_uid),
                     base::IntToString(files_gid));

  *applied_options = options;

  // Run rar2fs.
  // TODO(crbug.com/996549): Create a seccomp policy.
  FUSEMounter mounter(
      "rarfs", options, platform(), process_reaper(), "/usr/bin/rar2fs",
      "fuse-rar2fs", "" /* seccomp_policy */, GetBindPaths(source_path),
      false /* permit_network_access */, FUSEHelper::kFilesGroup);

  // To access Play Files.
  mounter.AddGroup("android-everybody");

  return mounter.Mount(source_path, mount_path, {}, error);
}

bool RarManager::Increment(const std::string::iterator begin,
                           std::string::iterator end) {
  while (true) {
    if (begin == end) {
      // Overflow.
      return false;
    }

    char& c = *--end;

    if (c == '9') {
      // Roll 9 to 0.
      c = '0';
    } else if (c == 'z') {
      // Roll z to a.
      c = 'a';
    } else if (c == 'Z') {
      // Roll Z to A.
      c = 'A';
    } else {
      // Increment any other character and done.
      ++c;
      return true;
    }
  }
}

RarManager::IndexRange RarManager::ParseDigits(base::StringPiece path) {
  const base::StringPiece extension = kExtension;

  if (!base::EndsWith(path, extension, base::CompareCase::INSENSITIVE_ASCII))
    return {};

  path.remove_suffix(extension.size());
  const size_t end = path.size();

  while (!path.empty() && base::IsAsciiDigit(path.back()))
    path.remove_suffix(1);

  if (!base::EndsWith(path, ".part", base::CompareCase::INSENSITIVE_ASCII))
    return {};

  return {path.size(), end};
}

void RarManager::AddPathsWithOldNamingScheme(
    std::vector<FUSEMounter::BindPath>* const bind_paths,
    const base::StringPiece original_path) const {
  DCHECK(bind_paths);

  // Is the extension right?
  if (!base::EndsWith(original_path, kExtension,
                      base::CompareCase::INSENSITIVE_ASCII))
    return;

  // Prepare candidate path.
  std::string candidate_path(original_path);
  const std::string::iterator end = candidate_path.end();

  // Set the last 2 characters to '0', so that extension '.rar' becomes '.r00'
  // and extension '.RAR' becomes '.R00'.
  std::fill(end - 2, end, '0');

  // Is there at least the first supplementary file of the multipart archive?
  if (!platform()->PathExists(candidate_path))
    return;

  bind_paths->push_back({candidate_path});

  // Iterate by incrementing the last 3 characters of the extension:
  // '.r00' -> '.r01' -> ... -> '.r99' -> '.s00' -> ... -> '.z99'
  // or
  // '.R00' -> '.R01' -> ... -> '.R99' -> '.S00' -> ... -> '.Z99'
  while (Increment(end - 3, end) && platform()->PathExists(candidate_path))
    bind_paths->push_back({candidate_path});
}

void RarManager::AddPathsWithNewNamingScheme(
    std::vector<FUSEMounter::BindPath>* const bind_paths,
    const base::StringPiece original_path,
    const IndexRange& digits) const {
  DCHECK(bind_paths);
  DCHECK_LT(digits.begin, digits.end);
  DCHECK_LE(digits.end, original_path.size());

  // Prepare candidate path.
  std::string candidate_path(original_path);

  // [begin, end) is the digit range to increment.
  const std::string::iterator begin = candidate_path.begin() + digits.begin;
  const std::string::iterator end = candidate_path.begin() + digits.end;

  // Fill the digit range with zeros.
  std::fill(begin, end, '0');

  // Find all the files making the multipart archive.
  while (Increment(begin, end) && platform()->PathExists(candidate_path)) {
    if (candidate_path != original_path)
      bind_paths->push_back({candidate_path});
  }
}

std::vector<FUSEMounter::BindPath> RarManager::GetBindPaths(
    const base::StringPiece original_path) const {
  std::vector<FUSEMounter::BindPath> bind_paths = {
      {std::string(original_path)}};

  // Delimit the digit range assuming original_path uses the new naming scheme.
  const IndexRange digits = ParseDigits(original_path);
  if (digits.empty()) {
    // Use the old naming scheme.
    AddPathsWithOldNamingScheme(&bind_paths, original_path);
  } else {
    // Use the new naming scheme.
    AddPathsWithNewNamingScheme(&bind_paths, original_path, digits);
  }

  return bind_paths;
}

}  // namespace cros_disks
