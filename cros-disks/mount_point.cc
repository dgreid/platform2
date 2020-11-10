// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <base/logging.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/mounter.h"
#include "cros-disks/quote.h"

namespace cros_disks {

namespace {

class LeakingMountPoint : public MountPoint {
 public:
  explicit LeakingMountPoint(const base::FilePath& path) : MountPoint(path) {}
  ~LeakingMountPoint() override { DestructorUnmount(); }
  LeakingMountPoint(const LeakingMountPoint&) = delete;
  LeakingMountPoint& operator=(const LeakingMountPoint&) = delete;

 protected:
  // MountPoint overrides.
  MountErrorType UnmountImpl() override { return MOUNT_ERROR_PATH_NOT_MOUNTED; }
};

}  // namespace

// static
std::unique_ptr<MountPoint> MountPoint::CreateLeaking(
    const base::FilePath& path) {
  return std::make_unique<LeakingMountPoint>(path);
}

MountPoint::MountPoint(const base::FilePath& path) : path_(path) {
  DCHECK(!path_.empty());
}

MountPoint::~MountPoint() {
  // Verify that subclasses call DestructorUnmount().
  DCHECK(unmounted_on_destruction_)
      << "MountPoint subclasses MUST call DestructorUnmount() in their "
      << "destructor";
}

void MountPoint::Release() {
  released_ = true;
}

MountErrorType MountPoint::Unmount() {
  if (released_) {
    // TODO(amistry): "not mounted" and "already unmounted" are logically
    // different and should be treated differently. Introduce a new error code
    // to represent this distinction.
    return MOUNT_ERROR_PATH_NOT_MOUNTED;
  }
  MountErrorType error = UnmountImpl();
  if (error == MOUNT_ERROR_NONE) {
    released_ = true;
  }
  return error;
}

void MountPoint::DestructorUnmount() {
  if (unmounted_on_destruction_) {
    return;
  }
  unmounted_on_destruction_ = true;

  MountErrorType error = Unmount();
  if (error != MOUNT_ERROR_NONE && error != MOUNT_ERROR_PATH_NOT_MOUNTED) {
    LOG(WARNING) << "Unmount error while destroying MountPoint("
                 << quote(path()) << "): " << error;
  }
}

}  // namespace cros_disks
