// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_SSHFS_HELPER_H_
#define CROS_DISKS_SSHFS_HELPER_H_

#include <memory>
#include <string>
#include <vector>

#include "cros-disks/fuse_helper.h"

namespace cros_disks {

// Invokes sshfs to provide access to files over SFTP protocol.
class SshfsHelper : public FUSEHelper {
 public:
  SshfsHelper(const Platform* platform, brillo::ProcessReaper* process_reaper);
  SshfsHelper(const SshfsHelper&) = delete;
  SshfsHelper& operator=(const SshfsHelper&) = delete;

  ~SshfsHelper() override;

  std::unique_ptr<FUSEMounter> CreateMounter(
      const base::FilePath& working_dir,
      const Uri& source,
      const base::FilePath& target_path,
      const std::vector<std::string>& options) const override;

 private:
  bool PrepareWorkingDirectory(const base::FilePath& working_dir,
                               uid_t uid,
                               gid_t gid,
                               std::vector<std::string>* options) const;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_SSHFS_HELPER_H_
