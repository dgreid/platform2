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
#include <base/files/file_path.h>
#include <base/strings/string_piece.h>

#include "cros-disks/metrics.h"
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

  using BindPaths = std::vector<BindPath>;

  // Parameters passed to FUSEMounter's constructor.
  // Members are kept in alphabetical order.
  struct Params {
    // Paths the FUSE mount program needs to access (beyond basic /proc, /dev,
    // etc).
    BindPaths bind_paths;

    // Filesystem type.
    std::string filesystem_type;

    // Optional object that collects UMA metrics.
    Metrics* metrics = nullptr;

    // Optional group to run the FUSE mount program as.
    std::string mount_group;

    // Optional mount namespace where the source path exists.
    std::string mount_namespace;

    // FUSE mount options.
    MountOptions mount_options;

    // Path of the FUSE mount program.
    std::string mount_program;

    // User to run the FUSE mount program as.
    std::string mount_user;

    // Whether the FUSE mount program needs to access the network.
    bool network_access = false;

    // Code returned by the FUSE mount program to ask for a password.
    int password_needed_code = -1;

    // Object that provides platform service.
    const Platform* platform = nullptr;

    // Process reaper to monitor FUSE daemons.
    brillo::ProcessReaper* process_reaper = nullptr;

    // Optional path to BPF seccomp filter policy.
    std::string seccomp_policy;

    // Supplementary groups to run the mount program with.
    std::vector<gid_t> supplementary_groups;
  };

  explicit FUSEMounter(Params params);

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

  // An object that collects UMA metrics.
  Metrics* const metrics_;

  // Path of the FUSE mount program.
  const std::string mount_program_;

  // User to run the FUSE mount program as.
  const std::string mount_user_;

  // Group to run the FUSE mount program as.
  const std::string mount_group_;

  // If not empty the path to BPF seccomp filter policy.
  const std::string seccomp_policy_;

  // Paths the FUSE mount program needs to access (beyond basic /proc, /dev,
  // etc).
  const BindPaths bind_paths_;

  // Whether to FUSE mount program needs to access the network.
  const bool network_access_;

  // If not empty, mount namespace where the source path exists.
  const std::string mount_namespace_;

  // Supplementary groups to run the FUSE mount program with.
  const std::vector<gid_t> supplementary_groups_;

  // Code returned by the FUSE mount program to ask for a password.
  const int password_needed_code_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FUSEMounter);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNTER_H_
