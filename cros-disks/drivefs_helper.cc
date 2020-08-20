// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/drivefs_helper.h"

#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/system_mounter.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

const char kDataDirOptionPrefix[] = "datadir=";
const char kIdentityOptionPrefix[] = "identity=";
const char kMyFilesOptionPrefix[] = "myfiles=";
const char kPathPrefixOptionPrefix[] = "prefix=";

const char kOldUser[] = "fuse-drivefs";
const char kHelperTool[] = "/opt/google/drive-file-stream/drivefs";
const char kSeccompPolicyFile[] =
    "/opt/google/drive-file-stream/drivefs-seccomp.policy";
const char kType[] = "drivefs";
const char kDbusSocketPath[] = "/run/dbus";

// The deepest expected path within a DriveFS datadir is
// {content,thumbnails}_cache/d<number>/d<number>/<number>. Allow one extra
// level just in case.
constexpr int kMaxTraversalDepth = 5;

// Ensures that the datadir has the correct owner. If not, recursively chown the
// contents, skipping directories with the expected owner. During directory
// descent, set group to the eventual group to allow directory traversal to
// allow access to the contents. On ascent, set user to the expected user,
// marking the directory as having the correct ownership.
bool EnsureOwnership(const Platform& platform,
                     const base::FilePath& path,
                     uid_t mounter_uid,
                     gid_t files_gid,
                     uid_t old_mounter_uid,
                     int depth = 0) {
  if (depth > kMaxTraversalDepth) {
    LOG(ERROR) << "Reached maximum traversal depth ensuring drivefs datadir "
                  "ownership: "
               << path.value();
    return false;
  }
  uid_t current_uid;
  gid_t current_gid;
  if (!platform.GetOwnership(path.value(), &current_uid, &current_gid)) {
    LOG(ERROR) << "Cannot access datadir " << quote(path);
    return false;
  }
  if (current_uid == mounter_uid) {
    return true;
  }
  if (current_uid != old_mounter_uid) {
    LOG(ERROR) << "Unexpected old uid for " << quote(path) << ": Expected "
               << old_mounter_uid << " but found " << current_uid;
    return false;
  }
  // Set group to |files_gid| to ensure the directory is traversable. Keep the
  // |current_uid| so this directory isn't treated as having the correct
  // ownership in case this operation is interrupted.
  if (!platform.SetOwnership(path.value(), current_uid, files_gid)) {
    LOG(ERROR) << "Cannot chown " << quote(path) << " to " << current_uid << ":"
               << files_gid;
    return false;
  }
  base::FileEnumerator dirs(path, false, base::FileEnumerator::DIRECTORIES);
  for (auto dir_path = dirs.Next(); !dir_path.empty(); dir_path = dirs.Next()) {
    if (!EnsureOwnership(platform, dir_path, mounter_uid, files_gid,
                         old_mounter_uid, depth + 1)) {
      return false;
    }
  }
  base::FileEnumerator files(path, false, base::FileEnumerator::FILES);
  for (auto file_path = files.Next(); !file_path.empty();
       file_path = files.Next()) {
    if (!platform.SetOwnership(file_path.value(), mounter_uid, files_gid)) {
      LOG(ERROR) << "Cannot chown " << quote(file_path) << " to " << mounter_uid
                 << ":" << files_gid;
      return false;
    }
  }
  if (!platform.SetOwnership(path.value(), mounter_uid, files_gid)) {
    LOG(ERROR) << "Cannot chown " << quote(path) << " to " << mounter_uid << ":"
               << files_gid;
    return false;
  }
  return true;
}

class DrivefsMounter : public FUSEMounter {
 public:
  DrivefsMounter(std::string filesystem_type,
                 MountOptions mount_options,
                 const Platform* platform,
                 brillo::ProcessReaper* process_reaper,
                 std::string mount_program,
                 std::string mount_user,
                 std::string seccomp_policy,
                 BindPaths bind_paths)
      : FUSEMounter({.bind_paths = std::move(bind_paths),
                     .filesystem_type = std::move(filesystem_type),
                     .mount_options = std::move(mount_options),
                     .mount_program = std::move(mount_program),
                     .mount_user = std::move(mount_user),
                     .network_access = true,
                     .platform = platform,
                     .process_reaper = process_reaper,
                     .seccomp_policy = seccomp_policy}) {}

  // FUSEMounter overrides:
  std::unique_ptr<MountPoint> Mount(const std::string& source,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> options,
                                    MountErrorType* error) const override {
    return FUSEMounter::Mount("", target_path, options, error);
  }
};

}  // namespace

DrivefsHelper::DrivefsHelper(const Platform* platform,
                             brillo::ProcessReaper* process_reaper)
    : FUSEHelper(kType,
                 platform,
                 process_reaper,
                 base::FilePath(kHelperTool),
                 kFilesUser) {}

DrivefsHelper::~DrivefsHelper() = default;

std::unique_ptr<FUSEMounter> DrivefsHelper::CreateMounter(
    const base::FilePath& working_dir,
    const Uri& source,
    const base::FilePath& target_path,
    const std::vector<std::string>& options) const {
  const std::string& identity = source.path();

  // Enforced by FUSEHelper::CanMount().
  DCHECK(!identity.empty());

  auto data_dir = GetValidatedDirectory(options, kDataDirOptionPrefix);
  if (data_dir.empty()) {
    return nullptr;
  }

  uid_t files_uid;
  gid_t files_gid;
  if (!platform()->GetUserAndGroupId(kFilesUser, &files_uid, nullptr) ||
      !platform()->GetGroupId(kFilesGroup, &files_gid)) {
    LOG(ERROR) << "Invalid user configuration.";
    return nullptr;
  }

  auto my_files_path = GetValidatedDirectory(options, kMyFilesOptionPrefix);
  if (!my_files_path.empty() && !CheckMyFilesPermissions(my_files_path)) {
    return nullptr;
  }

  if (!SetupDirectoryForFUSEAccess(data_dir)) {
    return nullptr;
  }
  MountOptions mount_options;
  mount_options.EnforceOption(kDataDirOptionPrefix + data_dir.value());
  mount_options.EnforceOption(kIdentityOptionPrefix + identity);
  mount_options.EnforceOption(kPathPrefixOptionPrefix + target_path.value());
  if (!my_files_path.empty()) {
    mount_options.EnforceOption(kMyFilesOptionPrefix + my_files_path.value());
  }
  mount_options.Initialize(options, true, base::NumberToString(files_uid),
                           base::NumberToString(files_gid));

  // TODO(crbug.com/859802): Make seccomp mandatory when testing done.
  std::string seccomp =
      platform()->PathExists(kSeccompPolicyFile) ? kSeccompPolicyFile : "";

  // Bind datadir and DBus communication socket into the sandbox.
  FUSEMounter::BindPaths paths = {{.path = data_dir.value(), .writable = true},
                                  {.path = kDbusSocketPath, .writable = true}};
  if (!my_files_path.empty()) {
    paths.push_back(
        {.path = my_files_path.value(), .writable = true, .recursive = true});
  }
  return std::make_unique<DrivefsMounter>(
      type(), mount_options, platform(), process_reaper(),
      program_path().value(), user(), seccomp, paths);
}

base::FilePath DrivefsHelper::GetValidatedDirectory(
    const std::vector<std::string>& options,
    const base::StringPiece prefix) const {
  for (const auto& option : options) {
    if (base::StartsWith(option, prefix, base::CompareCase::SENSITIVE)) {
      std::string path_string = option.substr(prefix.size());
      base::FilePath data_dir(path_string);
      if (data_dir.empty() || !data_dir.IsAbsolute() ||
          data_dir.ReferencesParent()) {
        LOG(ERROR) << "Invalid DriveFS option " << prefix << path_string;
        return {};
      }
      base::FilePath suffix_component;
      // If the datadir doesn't exist, canonicalize the parent directory
      // instead, and append the last path component to that path.
      if (!platform()->DirectoryExists(data_dir.value())) {
        suffix_component = data_dir.BaseName();
        data_dir = data_dir.DirName();
      }
      if (!platform()->GetRealPath(data_dir.value(), &path_string)) {
        return {};
      }
      return base::FilePath(path_string).Append(suffix_component);
    }
  }
  return {};
}

bool DrivefsHelper::SetupDirectoryForFUSEAccess(
    const base::FilePath& dir) const {
  CHECK(dir.IsAbsolute() && !dir.ReferencesParent())
      << "Unsafe path " << quote(dir);

  uid_t mounter_uid, old_mounter_uid;
  gid_t files_gid;
  if (!platform()->GetUserAndGroupId(user(), &mounter_uid, nullptr) ||
      !platform()->GetUserAndGroupId(kOldUser, &old_mounter_uid, nullptr) ||
      !platform()->GetGroupId(kFilesGroup, &files_gid)) {
    LOG(ERROR) << "Invalid user configuration.";
    return false;
  }

  std::string path = dir.value();
  if (platform()->DirectoryExists(path)) {
    return EnsureOwnership(*platform(), dir, mounter_uid, files_gid,
                           old_mounter_uid);
  }
  if (!platform()->CreateDirectory(path)) {
    LOG(ERROR) << "Cannot create datadir " << quote(path);
    return false;
  }
  if (!platform()->SetPermissions(path, 0770)) {
    LOG(ERROR) << "Cannot chmod datadir " << quote(path);
    return false;
  }
  if (!platform()->SetOwnership(path, mounter_uid, files_gid)) {
    LOG(ERROR) << "Cannot chown datadir " << quote(path);
    return false;
  }
  return true;
}

bool DrivefsHelper::CheckMyFilesPermissions(const base::FilePath& dir) const {
  CHECK(dir.IsAbsolute() && !dir.ReferencesParent())
      << "Unsafe 'My Files' path " << quote(dir);

  uid_t mounter_uid;
  if (!platform()->GetUserAndGroupId(user(), &mounter_uid, nullptr)) {
    LOG(ERROR) << "Invalid user configuration.";
    return false;
  }

  std::string path = dir.value();
  if (!platform()->DirectoryExists(path)) {
    LOG(ERROR) << "My files directory " << quote(path) << " does not exist";
    return false;
  }
  uid_t current_uid;
  if (!platform()->GetOwnership(path, &current_uid, nullptr)) {
    LOG(WARNING) << "Cannot access my files directory " << quote(path);
    return false;
  }
  if (current_uid != mounter_uid) {
    LOG(ERROR) << "Incorrect owner for my files directory " << quote(path);
    return false;
  }
  return true;
}

}  // namespace cros_disks
