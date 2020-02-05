// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_RAR_MANAGER_H_
#define CROS_DISKS_RAR_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/strings/string_piece.h>
#include <gtest/gtest_prod.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_manager.h"

namespace cros_disks {

// A MountManager mounting RAR archives as virtual filesystems using rar2fs.
class RarManager : public MountManager {
 public:
  RarManager(const std::string& mount_root,
             Platform* platform,
             Metrics* metrics,
             brillo::ProcessReaper* reaper);

  ~RarManager() override;

 private:
  // MountManager overrides
  MountSourceType GetMountSourceType() const override {
    return MOUNT_SOURCE_ARCHIVE;
  }

  bool CanMount(const std::string& source_path) const override;

  std::string SuggestMountPath(const std::string& source_path) const override;

  std::unique_ptr<MountPoint> DoMount(const std::string& source_path,
                                      const std::string& filesystem_type,
                                      const std::vector<std::string>& options,
                                      const base::FilePath& mount_path,
                                      MountOptions* applied_options,
                                      MountErrorType* error) override;

  // Prepares the bind paths for the given RAR file path.
  // TODO(crbug.com/221124): Handle multipart archives.
  std::vector<FUSEMounter::BindPath> GetBindPaths(base::StringPiece s) const;

  FRIEND_TEST(RarManagerTest, CanMount);
  FRIEND_TEST(RarManagerTest, SuggestMountPath);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_RAR_MANAGER_H_
