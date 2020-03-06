// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FUSE_MOUNTER_H_
#define CROS_DISKS_FUSE_MOUNTER_H_

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/strings/string_piece.h>

#include "cros-disks/mounter.h"

namespace brillo {
class ProcessReaper;
}  // namespace brillo

namespace cros_disks {

class Platform;
class SandboxedProcess;

// A class for mounting a device file using a FUSE mount program.
class FUSEMounter : public MounterCompat {
 public:
  struct BindPath {
    std::string path;
    bool writable = false;
    bool recursive = false;
  };

  FUSEMounter(const std::string& filesystem_type,
              const MountOptions& mount_options,
              const Platform* platform,
              brillo::ProcessReaper* process_reaper,
              const std::string& mount_program_path,
              const std::string& mount_user,
              const std::string& seccomp_policy,
              const std::vector<BindPath>& accessible_paths,
              bool permit_network_access,
              const std::string& mount_group = {});

  // Adds a supplementary group to run the FUSE mount program with.
  // Returns whether the given group exists.
  [[nodiscard]] bool AddGroup(const std::string& group);

  // MounterCompat overrides.
  std::unique_ptr<MountPoint> Mount(const std::string& source,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> options,
                                    MountErrorType* error) const override;

 protected:
  // Protected for mocking out in testing.
  virtual std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const;

  // An object that provides platform service.
  const Platform* const platform_;

  // An object to monitor FUSE daemons.
  brillo::ProcessReaper* const process_reaper_;

  // Path of the FUSE mount program.
  const std::string mount_program_path_;

  // User to run the FUSE mount program as.
  const std::string mount_user_;

  // Group to run the FUSE mount program as.
  const std::string mount_group_;

  // If not empty the path to BPF seccomp filter policy.
  const std::string seccomp_policy_;

  // Directories the FUSE module should be able to access (beyond basic
  // /proc, /dev, etc).
  const std::vector<BindPath> accessible_paths_;

  // Whether to leave network access to the mount program.
  const bool permit_network_access_;

  // Supplementary groups to run the FUSE mount program with.
  std::vector<gid_t> supplementary_groups_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FUSEMounter);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNTER_H_
