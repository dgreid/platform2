// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/system_mounter.h"

#include <errno.h>
#include <sys/mount.h>

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/containers/util.h>
#include <base/strings/string_util.h>

#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"

namespace cros_disks {

namespace {

constexpr MountOptions::Flags kExternalDiskMountFlags =
    MountOptions::kMountFlags | MS_NOSYMFOLLOW | MS_DIRSYNC;

// A MountPoint that uses the umount() syscall for unmounting.
class SystemMountPoint : public MountPoint {
 public:
  SystemMountPoint(const base::FilePath& path, const Platform* platform)
      : MountPoint(path), platform_(platform) {}

  SystemMountPoint(const SystemMountPoint&) = delete;
  SystemMountPoint& operator=(const SystemMountPoint&) = delete;

  ~SystemMountPoint() override { DestructorUnmount(); }

 protected:
  MountErrorType UnmountImpl() override {
    MountErrorType error = platform_->Unmount(path().value(), 0);
    if (error == MOUNT_ERROR_PATH_ALREADY_MOUNTED) {
      LOG(WARNING) << "Device is busy, trying lazy unmount on "
                   << path().value();
      error = platform_->Unmount(path().value(), MNT_DETACH);
    }
    return error;
  }

 private:
  const Platform* platform_;
};

}  // namespace

SystemMounter::SystemMounter(const Platform* platform,
                             std::string filesystem_type,
                             bool read_only,
                             std::vector<std::string> options)
    : platform_(platform),
      filesystem_type_(std::move(filesystem_type)),
      flags_(kExternalDiskMountFlags | (read_only ? MS_RDONLY : 0)),
      options_(std::move(options)) {}

SystemMounter::~SystemMounter() = default;

std::unique_ptr<MountPoint> SystemMounter::Mount(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    MountErrorType* error) const {
  int flags = flags_;

  // We only care about "ro" here.
  if (IsReadOnlyMount(params)) {
    flags |= MS_RDONLY;
  }

  std::vector<std::string> options = options_;
  *error = ParseParams(std::move(params), &options);
  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }

  *error = platform_->Mount(source, target_path.value(), filesystem_type_,
                            flags, base::JoinString(options, ","));
  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }

  return std::make_unique<SystemMountPoint>(target_path, platform_);
}

bool SystemMounter::CanMount(const std::string& source,
                             const std::vector<std::string>& /*params*/,
                             base::FilePath* suggested_dir_name) const {
  if (source.empty()) {
    *suggested_dir_name = base::FilePath("disk");
  } else {
    *suggested_dir_name = base::FilePath(source).BaseName();
  }
  return true;
}

MountErrorType SystemMounter::ParseParams(
    std::vector<std::string> /*params*/,
    std::vector<std::string>* /*mount_options*/) const {
  return MOUNT_ERROR_NONE;
}

}  // namespace cros_disks
