// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/platform.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>

#include "cros-disks/quote.h"

namespace cros_disks {

Platform::Platform()
    : mount_group_id_(0), mount_user_id_(0), mount_user_("root") {}

bool Platform::GetRealPath(const std::string& path,
                           std::string* real_path) const {
  CHECK(real_path) << "Invalid real_path argument";

  std::unique_ptr<char, base::FreeDeleter> result(
      realpath(path.c_str(), nullptr));
  if (!result) {
    PLOG(ERROR) << "Cannot get real path of " << quote(path);
    return false;
  }

  *real_path = result.get();
  return true;
}

bool Platform::PathExists(const std::string& path) const {
  return base::PathExists(base::FilePath(path));
}

bool Platform::DirectoryExists(const std::string& path) const {
  return base::DirectoryExists(base::FilePath(path));
}

bool Platform::Lstat(const std::string& path, base::stat_wrapper_t* out) const {
  return base::File::Lstat(path.c_str(), out) == 0;
}

bool Platform::CreateDirectory(const std::string& path) const {
  if (!base::CreateDirectory(base::FilePath(path))) {
    LOG(ERROR) << "Cannot create directory " << quote(path);
    return false;
  }
  LOG(INFO) << "Created directory " << quote(path);
  return true;
}

bool Platform::CreateOrReuseEmptyDirectory(const std::string& path) const {
  CHECK(!path.empty()) << "Invalid path argument";

  // Reuse the target path if it already exists and is empty.
  // rmdir handles the cases when the target path exists but
  // is not empty, is already mounted or is used by some process.
  rmdir(path.c_str());
  if (mkdir(path.c_str(), S_IRWXU) != 0) {
    PLOG(ERROR) << "Cannot create directory " << quote(path);
    return false;
  }
  return true;
}

bool Platform::CreateOrReuseEmptyDirectoryWithFallback(
    std::string* path,
    unsigned max_suffix_to_retry,
    const std::set<std::string>& reserved_paths) const {
  CHECK(path && !path->empty()) << "Invalid path argument";

  if (!base::Contains(reserved_paths, *path) &&
      CreateOrReuseEmptyDirectory(*path))
    return true;

  for (unsigned suffix = 1; suffix <= max_suffix_to_retry; ++suffix) {
    std::string fallback_path = GetDirectoryFallbackName(*path, suffix);
    if (!base::Contains(reserved_paths, fallback_path) &&
        CreateOrReuseEmptyDirectory(fallback_path)) {
      *path = fallback_path;
      return true;
    }
  }
  return false;
}

bool Platform::CreateTemporaryDirInDir(const std::string& dir,
                                       const std::string& prefix,
                                       std::string* path) const {
  base::FilePath dest;
  bool result =
      base::CreateTemporaryDirInDir(base::FilePath(dir), prefix, &dest);
  if (result && path)
    *path = dest.value();
  return result;
}

int Platform::WriteFile(const std::string& file,
                        const char* data,
                        int size) const {
  return base::WriteFile(base::FilePath(file), data, size);
}

int Platform::ReadFile(const std::string& file, char* data, int size) const {
  return base::ReadFile(base::FilePath(file), data, size);
}

std::string Platform::GetDirectoryFallbackName(const std::string& path,
                                               unsigned suffix) const {
  if (!path.empty() && base::IsAsciiDigit(path[path.size() - 1]))
    return base::StringPrintf("%s (%u)", path.c_str(), suffix);

  return base::StringPrintf("%s %u", path.c_str(), suffix);
}

bool Platform::GetGroupId(const std::string& group_name,
                          gid_t* group_id) const {
  return brillo::userdb::GetGroupInfo(group_name, group_id);
}

bool Platform::GetUserAndGroupId(const std::string& user_name,
                                 uid_t* user_id,
                                 gid_t* group_id) const {
  return brillo::userdb::GetUserInfo(user_name, user_id, group_id);
}

bool Platform::GetOwnership(const std::string& path,
                            uid_t* user_id,
                            gid_t* group_id) const {
  struct stat path_status;
  if (stat(path.c_str(), &path_status) != 0) {
    PLOG(ERROR) << "Cannot get ownership info for " << quote(path);
    return false;
  }

  if (user_id)
    *user_id = path_status.st_uid;

  if (group_id)
    *group_id = path_status.st_gid;

  return true;
}

bool Platform::GetPermissions(const std::string& path, mode_t* mode) const {
  CHECK(mode) << "Invalid mode argument";

  struct stat path_status;
  if (stat(path.c_str(), &path_status) != 0) {
    PLOG(ERROR) << "Cannot get the permissions of " << quote(path);
    return false;
  }
  *mode = path_status.st_mode;
  return true;
}

bool Platform::SetMountUser(const std::string& user_name) {
  if (GetUserAndGroupId(user_name, &mount_user_id_, &mount_group_id_)) {
    mount_user_ = user_name;
    return true;
  }
  return false;
}

bool Platform::RemoveEmptyDirectory(const std::string& path) const {
  if (rmdir(path.c_str()) != 0) {
    PLOG(ERROR) << "Cannot remove directory " << quote(path);
    return false;
  }
  return true;
}

bool Platform::SetOwnership(const std::string& path,
                            uid_t user_id,
                            gid_t group_id) const {
  if (chown(path.c_str(), user_id, group_id)) {
    PLOG(ERROR) << "Cannot set ownership of " << quote(path) << " to uid "
                << user_id << " and gid " << group_id;
    return false;
  }
  return true;
}

bool Platform::SetPermissions(const std::string& path, mode_t mode) const {
  if (chmod(path.c_str(), mode)) {
    PLOG(ERROR) << "Cannot set permissions of " << quote(path) << " to "
                << base::StringPrintf("%04o", mode);
    return false;
  }
  return true;
}

MountErrorType Platform::Unmount(const std::string& path, int flags) const {
  error_t error = 0;
  if (umount2(path.c_str(), flags) != 0) {
    error = errno;
    PLOG(ERROR) << "Cannot unmount " << quote(path) << " with flags " << flags;
  } else {
    LOG(INFO) << "Unmounted " << quote(path) << " with flags " << flags;
  }
  switch (error) {
    case 0:
      return MOUNT_ERROR_NONE;
    case ENOENT:
      return MOUNT_ERROR_PATH_NOT_MOUNTED;
    case EPERM:
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    case EBUSY:
      return MOUNT_ERROR_PATH_ALREADY_MOUNTED;
    default:
      return MOUNT_ERROR_UNKNOWN;
  }
}

MountErrorType Platform::Mount(const std::string& source_path,
                               const std::string& target_path,
                               const std::string& filesystem_type,
                               const uint64_t flags,
                               const std::string& options) const {
  // Pass the nosymfollow option as both a flag and a string option for
  // compatibility across kernels.  The mount syscall ignores unknown flags,
  // so kernels that don't have MS_NOSYMFOLLOW will pick up nosymfollow from
  // the data parameter through the chromiumos LSM.  Kernels that do have
  // MS_NOSYMFOLLOW will pick up the same behavior directly from the flag;
  // our LSM ignores the string option in that case.
  //
  // TODO(b/152074038): Remove the string option once all devices have been
  // upreved to a kernel that supports MS_NOSYMFOLLOW (currently 5.4+).
  std::string mount_options = options;
  if ((flags & MS_NOSYMFOLLOW) == MS_NOSYMFOLLOW) {
    if (!mount_options.empty()) {
      mount_options += ",";
    }
    mount_options += "nosymfollow";
  }

  error_t error = 0;
  if (mount(source_path.c_str(), target_path.c_str(), filesystem_type.c_str(),
            flags, mount_options.c_str()) != 0) {
    error = errno;
    PLOG(ERROR) << "Cannot create mount point " << quote(target_path) << " for "
                << quote(source_path) << " as filesystem "
                << quote(filesystem_type) << " with flags 0x" << std::hex
                << flags << " and options " << quote(mount_options);
  } else {
    LOG(INFO) << "Created mount point " << quote(target_path) << " for "
              << quote(source_path) << " as filesystem "
              << quote(filesystem_type) << " with flags 0x" << std::hex << flags
              << " and options " << quote(mount_options);
  }

  switch (error) {
    case 0:
      return MOUNT_ERROR_NONE;
    case ENODEV:
      return MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
    case ENOENT:
    case ENOTBLK:
    case ENOTDIR:
      return MOUNT_ERROR_INVALID_PATH;
    case EPERM:
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    default:
      return MOUNT_ERROR_UNKNOWN;
  }
}

}  // namespace cros_disks
