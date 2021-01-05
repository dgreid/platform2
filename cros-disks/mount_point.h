// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_MOUNT_POINT_H_
#define CROS_DISKS_MOUNT_POINT_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <chromeos/dbus/service_constants.h>

namespace cros_disks {

// Holds information about a mount point.
struct MountPointData {
  // Mount point path.
  base::FilePath mount_path;
  // Source description used to mount.
  std::string source = {};
  // Filesystem type of the mount.
  std::string filesystem_type = {};
  // Flags of the mount point.
  int flags = 0;
  // Additional data passed during mount.
  std::string data = {};
};

// Class representing a mount created by a mounter.
class MountPoint {
 public:
  // Creates a MountPoint that does nothing on unmount and 'leaks' the mount
  // point.
  static std::unique_ptr<MountPoint> CreateLeaking(const base::FilePath& path);

  MountPoint(const MountPoint&) = delete;
  MountPoint& operator=(const MountPoint&) = delete;

  // Unmounts the mount point. Subclasses MUST call DestructorUnmount().
  virtual ~MountPoint();

  // Releases (leaks) the ownership of the mount point.
  // Until all places handle ownership of mount points properly
  // it's necessary to be able to leave the mount alone.
  virtual void Release();

  // Unmounts right now using the unmounter.
  MountErrorType Unmount();

  const base::FilePath& path() const { return data_.mount_path; }

 protected:
  // Protected constructor for subclasses.
  explicit MountPoint(MountPointData data);

  // Unmounts the point point and logs errors as appropriate. MUST be called in
  // the destructor.
  void DestructorUnmount();

  // Unmounts the mount point. If MOUNT_ERROR_NONE is returned, will only be
  // called once, regardless of the number of times Unmount() is called. If
  // Release() is called, this function will not be called.
  virtual MountErrorType UnmountImpl() = 0;

 private:
  const MountPointData data_;
  bool released_ = false;
  bool unmounted_on_destruction_ = false;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_MOUNT_POINT_H_
