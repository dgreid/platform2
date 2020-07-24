// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ARCHIVE_MANAGER_H_
#define CROS_DISKS_ARCHIVE_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <brillo/scoped_mount_namespace.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/mount_manager.h"
#include "cros-disks/mount_options.h"

namespace cros_disks {

// A derived class of MountManager for mounting archive files as a virtual
// filesystem.
class ArchiveManager : public MountManager {
 public:
  using MountManager::MountManager;

  // MountManager overrides
  MountSourceType GetMountSourceType() const final {
    return MOUNT_SOURCE_ARCHIVE;
  }

  bool ResolvePath(const std::string& path, std::string* real_path) final;

  std::string SuggestMountPath(const std::string& source_path) const final;

  // Checks if the given file path is in an allowed location to be mounted as an
  // archive. The following paths can be mounted:
  //
  //     /home/chronos/u-<user-id>/MyFiles/...<file>
  //     /media/archive/<dir>/...<file>
  //     /media/fuse/<dir>/...<file>
  //     /media/removable/<dir>/...<file>
  //     /run/arc/sdcard/write/emulated/0/<dir>/...<file>
  static bool IsInAllowedFolder(const std::string& source_path);

  // Gets a list of supplementary group IDs the FUSE mounter program should run
  // with in order to access files in all the required locations.
  std::vector<gid_t> GetSupplementaryGroups() const;

  // Gets FUSE mount options.
  MountErrorType GetMountOptions(MountOptions* options) const;

 protected:
  struct MountNamespace {
    std::unique_ptr<brillo::ScopedMountNamespace> guard;
    std::string name;
  };

  static MountNamespace GetMountNamespaceFor(const std::string& path);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_ARCHIVE_MANAGER_H_
