// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ZIP_MANAGER_H_
#define CROS_DISKS_ZIP_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "cros-disks/archive_manager.h"

namespace cros_disks {

// A MountManager mounting ZIP archives as virtual filesystems using fuse-zip.
class ZipManager : public ArchiveManager {
 public:
  using ArchiveManager::ArchiveManager;

  ~ZipManager() override;

 private:
  // ArchiveManager overrides
  bool CanMount(const std::string& source_path) const override;

  std::unique_ptr<MountPoint> DoMount(const std::string& source_path,
                                      const std::string& filesystem_type,
                                      const std::vector<std::string>& options,
                                      const base::FilePath& mount_path,
                                      MountOptions* applied_options,
                                      MountErrorType* error) override;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_ZIP_MANAGER_H_
