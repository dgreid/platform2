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
class Process;
class SandboxedProcess;

// Uprivileged mounting of any FUSE filesystem. Filesystem-specific set up
// and sandboxing is to be done in a subclass.
class FUSEMounter : public Mounter {
 public:
  FUSEMounter(const Platform* platform,
              brillo::ProcessReaper* process_reaper,
              std::string filesystem_type,
              bool nosymfollow);
  FUSEMounter(const FUSEMounter&) = delete;
  FUSEMounter& operator=(const FUSEMounter&) = delete;
  ~FUSEMounter() override;

  const Platform* platform() const { return platform_; }
  brillo::ProcessReaper* process_reaper() const { return process_reaper_; }

  // Mounter overrides:
  std::unique_ptr<MountPoint> Mount(const std::string& source,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> params,
                                    MountErrorType* error) const final;

 protected:
  // Performs necessary set up and launches FUSE daemon that communicates to
  // FUSE kernel layer via the |fuse_file|. Returns PID of the daemon process.
  virtual pid_t StartDaemon(const base::File& fuse_file,
                            const std::string& source,
                            const base::FilePath& target_path,
                            std::vector<std::string> params,
                            MountErrorType* error) const = 0;

 private:
  const Platform* const platform_;
  brillo::ProcessReaper* const process_reaper_;
  const std::string filesystem_type_;
  const bool nosymfollow_;
};

// A class for mounting something using a FUSE mount program.
// TODO(dats): It contains too much logic used only in some cases but
// not others. Tear it apart.
class FUSEMounterLegacy : public FUSEMounter {
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

    // Name of the UMA histogram recording the FUSE mount program return code.
    // Not recorded if empty or if metrics is null.
    std::string metrics_name;

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

    // By default it's mounted with symlinks following disabled.
    bool nosymfollow = true;

    // Possible codes returned by the FUSE mount program to ask for a password.
    std::vector<int> password_needed_codes;

    // Object that provides platform service.
    const Platform* platform = nullptr;

    // Process reaper to monitor FUSE daemons.
    brillo::ProcessReaper* process_reaper = nullptr;

    // Optional path to BPF seccomp filter policy.
    std::string seccomp_policy;

    // Supplementary groups to run the mount program with.
    std::vector<gid_t> supplementary_groups;
  };

  explicit FUSEMounterLegacy(Params params);
  FUSEMounterLegacy(const FUSEMounterLegacy&) = delete;
  FUSEMounterLegacy& operator=(const FUSEMounterLegacy&) = delete;

  // If necessary, extracts the password from the given options and sets the
  // standard input of the given process. Does nothing if password_needed_codes
  // is empty. Does nothing if no string starting with "password=" is found in
  // options. If several options start with "password=", only the first one is
  // taken in account and the other ones are ignored.
  void CopyPassword(const std::vector<std::string>& options,
                    Process* process) const;

  const MountOptions& mount_options() const { return mount_options_; }

 protected:
  // FUSEMounter overrides:
  bool CanMount(const std::string& source,
                const std::vector<std::string>& params,
                base::FilePath* suggested_name) const override;

  pid_t StartDaemon(const base::File& fuse_file,
                    const std::string& source,
                    const base::FilePath& target_path,
                    std::vector<std::string> params,
                    MountErrorType* error) const override;

  // Protected for mocking out in testing.
  virtual std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const;

  // An object that collects UMA metrics.
  Metrics* const metrics_;

  // Name of the UMA histogram recording the FUSE mount program return code.
  // Not recorded if empty or if metrics is null.
  const std::string metrics_name_;

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

  // Possible codes returned by the FUSE mount program to ask for a password.
  std::vector<int> password_needed_codes_;

  const MountOptions mount_options_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNTER_H_
