// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "cros-disks/archive_mounter.h"
#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {
namespace {

const char kExtension[] = ".rar";

OwnerUser GetRarUserOrDie(const Platform* platform) {
  OwnerUser run_as;
  PCHECK(platform->GetUserAndGroupId("fuse-rar2fs", &run_as.uid, &run_as.gid))
      << "Cannot resolve required user fuse-rar2fs";
  return run_as;
}

}  // namespace

class RarManager::RarMounter : public ArchiveMounter {
 public:
  RarMounter(const Platform* platform,
             brillo::ProcessReaper* process_reaper,
             Metrics* metrics,
             const RarManager* rar_manager)
      : ArchiveMounter(
            platform,
            process_reaper,
            "rar",
            metrics,
            "Rar2fs",
            {12,   // ERAR_BAD_DATA
             22,   // ERAR_MISSING_PASSWORD
             24},  // ERAR_BAD_PASSWORD
            std::make_unique<FUSESandboxedProcessFactory>(
                platform,
                SandboxedExecutable{
                    base::FilePath("/usr/bin/rar2fs"),
                    base::FilePath("/usr/share/policy/rar2fs-seccomp.policy")},
                GetRarUserOrDie(platform),
                /* has_network_access= */ false,
                rar_manager->GetSupplementaryGroups())),
        rar_manager_(rar_manager) {}

 protected:
  MountErrorType FormatInvocationCommand(
      const base::FilePath& archive,
      std::vector<std::string> params,
      SandboxedProcess* sandbox) const override {
    // Bind-mount parts of a multipart archive if any.
    for (const auto& path : rar_manager_->GetBindPaths(archive.value())) {
      if (!sandbox->BindMount(path, path, /* writeable= */ false,
                              /* recursive= */ false)) {
        PLOG(ERROR) << "Could not bind " << quote(path);
        return MOUNT_ERROR_INTERNAL;
      }
    }

    std::vector<std::string> opts = {
        MountOptions::kOptionReadOnly, "umask=0222", "locale=en_US.UTF8",
        base::StringPrintf("uid=%d", kChronosUID),
        base::StringPrintf("gid=%d", kChronosAccessGID)};

    sandbox->AddArgument("-o");
    sandbox->AddArgument(base::JoinString(opts, ","));
    sandbox->AddArgument(archive.value());

    return MOUNT_ERROR_NONE;
  }

  const RarManager* rar_manager_;
};

RarManager::RarManager(const std::string& mount_root,
                       Platform* platform,
                       Metrics* metrics,
                       brillo::ProcessReaper* process_reaper)
    : ArchiveManager(mount_root, platform, metrics, process_reaper),
      mounter_(std::make_unique<RarMounter>(
          platform, process_reaper, metrics, this)) {}

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
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(error);
  // MountManager resolves source path to real path before calling DoMount,
  // so no symlinks or '..' will be here.
  if (!IsInAllowedFolder(source_path)) {
    LOG(ERROR) << "Source path " << quote(source_path) << " is not allowed";
    *error = MOUNT_ERROR_INVALID_DEVICE_PATH;
    return nullptr;
  }
  return mounter_->Mount(source_path, mount_path, options, error);
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

  return {path.size(), end};
}

void RarManager::AddPathsWithOldNamingScheme(
    std::vector<std::string>* const bind_paths,
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

  bind_paths->push_back(candidate_path);

  // Iterate by incrementing the last 3 characters of the extension:
  // '.r00' -> '.r01' -> ... -> '.r99' -> '.s00' -> ... -> '.z99'
  // or
  // '.R00' -> '.R01' -> ... -> '.R99' -> '.S00' -> ... -> '.Z99'
  while (Increment(end - 3, end) && platform()->PathExists(candidate_path))
    bind_paths->push_back(candidate_path);
}

void RarManager::AddPathsWithNewNamingScheme(
    std::vector<std::string>* const bind_paths,
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
      bind_paths->push_back(candidate_path);
  }
}

std::vector<std::string> RarManager::GetBindPaths(
    const base::StringPiece original_path) const {
  std::vector<std::string> bind_paths = {std::string(original_path)};

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
