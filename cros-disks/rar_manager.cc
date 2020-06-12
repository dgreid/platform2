// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <algorithm>
#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/scoped_mount_namespace.h>

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

RarManager::~RarManager() {
  UnmountAll();
}

bool RarManager::CanMount(const std::string& source_path) const {
  // Check for expected file extension.
  return base::EndsWith(source_path, kExtension,
                        base::CompareCase::INSENSITIVE_ASCII) &&
         IsInAllowedFolder(source_path);
}

std::unique_ptr<MountPoint> RarManager::DoMount(
    const std::string& source_path,
    const std::string& filesystem_type,
    const std::vector<std::string>& original_options,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(applied_options);
  DCHECK(error);

  if (filesystem_type != ".rar2fs")
    return ArchiveManager::DoMount(source_path, filesystem_type,
                                   original_options, mount_path,
                                   applied_options, error);

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
  options.WhitelistOptionPrefix("locale=");
  options.WhitelistOptionPrefix("umask=");
  options.Initialize(
      {
          "locale=en_US.UTF8",
          "umask=0222",  // umask in octal: 0222 == r-x r-x r-x
          MountOptions::kOptionReadOnly,
      },
      true, base::NumberToString(files_uid), base::NumberToString(files_gid));

  *applied_options = options;

  // Mount namespace to use when running rar2fs.
  std::string mount_namespace_path;
  std::vector<FUSEMounter::BindPath> bind_paths;

  // Determine which mount namespace to use.
  {
    // Attempt to enter the Chrome mount namespace, if it exists.
    auto guard = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(kChromeMountNamespacePath));

    if (guard) {
      // Check if the source path exists in Chrome's mount namespace.
      if (base::PathExists(base::FilePath(source_path))) {
        // The source path exists in Chrome's mount namespace.
        mount_namespace_path = kChromeMountNamespacePath;
      } else {
        // Use the default mount namespace.
        guard.reset();
      }
    }

    bind_paths = GetBindPaths(source_path);
  }

  // Run rar2fs.
  FUSEMounter mounter("rarfs", options, platform(), process_reaper(),
                      "/usr/bin/rar2fs", "fuse-rar2fs",
                      "/usr/share/policy/rar2fs-seccomp.policy", bind_paths,
                      false /* permit_network_access */,
                      FUSEHelper::kFilesGroup, mount_namespace_path, metrics());

  // To access Play Files.
  if (!mounter.AddGroup("android-everybody"))
    LOG(INFO) << "Group 'android-everybody' does not exist";

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
