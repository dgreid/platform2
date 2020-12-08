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
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/user.h"

namespace brillo {
class ProcessReaper;
}  // namespace brillo

namespace cros_disks {

class Platform;
class Process;
class SandboxedProcess;

// Class for creating instances of SandboxedProcess with appropriate
// configuration.
class FUSESandboxedProcessFactory : public SandboxedProcessFactory {
 public:
  FUSESandboxedProcessFactory(
      const Platform* platform,
      SandboxedExecutable executable,
      OwnerUser run_as,
      bool has_network_access = false,
      std::vector<gid_t> supplementary_groups = {},
      base::Optional<base::FilePath> mount_namespace = {});
  ~FUSESandboxedProcessFactory() override;

  // Returns pre-configured sandbox with the most essential set up. Additional
  // per-instance configuration should be done by the caller if needed.
  std::unique_ptr<SandboxedProcess> CreateSandboxedProcess() const override;

  const base::FilePath& executable() const { return executable_; }
  const OwnerUser& run_as() const { return run_as_; }

 private:
  friend class FUSESandboxedProcessFactoryTest;

  bool ConfigureSandbox(SandboxedProcess* sandbox) const;

  const Platform* const platform_;

  // Path to the FUSE daemon executable.
  const base::FilePath executable_;

  // Path to the seccomp policy configuration.
  const base::Optional<base::FilePath> seccomp_policy_;

  // UID/GID to run the FUSE daemon as.
  const OwnerUser run_as_;

  // Whether to leave network accessible from the sandbox.
  const bool has_network_access_;

  // Additional groups to associate with the FUSE daemon process.
  const std::vector<gid_t> supplementary_groups_;

  // Path identifying the mount namespace to use.
  const base::Optional<base::FilePath> mount_namespace_;
};

// Uprivileged mounting of any FUSE filesystem. Filesystem-specific set up
// and sandboxing is to be done in a subclass.
class FUSEMounter : public Mounter {
 public:
  struct Config {
    bool nosymfollow = true;
    bool read_only = false;
  };

  FUSEMounter(const Platform* platform,
              brillo::ProcessReaper* process_reaper,
              std::string filesystem_type,
              Config config);
  FUSEMounter(const FUSEMounter&) = delete;
  FUSEMounter& operator=(const FUSEMounter&) = delete;
  ~FUSEMounter() override;

  const Platform* platform() const { return platform_; }
  brillo::ProcessReaper* process_reaper() const { return process_reaper_; }
  const std::string& filesystem_type() const { return filesystem_type_; }

  // Mounter overrides:
  std::unique_ptr<MountPoint> Mount(const std::string& source,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> params,
                                    MountErrorType* error) const final;

 protected:
  // Translates mount app's return codes into errors. The base
  // implementation just assumes any non-zero return code to be a
  // MOUNT_ERROR_MOUNT_PROGRAM_FAILED, but subclasses can implement more
  // elaborate mappings.
  virtual MountErrorType InterpretReturnCode(int return_code) const;

  // Performs necessary set up and makes a SandboxedProcess ready to be
  // launched to serve a mount. The returned instance will have one more
  // last argument added to indicate the FUSE mount path according to
  // fusermount's conventions, so implementation doesn't have to do this,
  // |target_path| is purely informational.
  virtual std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath& target_path,
      std::vector<std::string> params,
      MountErrorType* error) const = 0;

 private:
  // Performs necessary set up and launches FUSE daemon that communicates to
  // FUSE kernel layer via the |fuse_file|. Returns PID of the daemon process.
  pid_t StartDaemon(const base::File& fuse_file,
                    const std::string& source,
                    const base::FilePath& target_path,
                    std::vector<std::string> params,
                    MountErrorType* error) const;

 private:
  const Platform* const platform_;
  brillo::ProcessReaper* const process_reaper_;
  const std::string filesystem_type_;
  const Config config_;
};

// A convenience class to tie FUSE mounter with a sandbox configuration.
class FUSEMounterHelper : public FUSEMounter {
 public:
  FUSEMounterHelper(const Platform* platform,
                    brillo::ProcessReaper* process_reaper,
                    std::string filesystem_type,
                    bool nosymfollow,
                    const SandboxedProcessFactory* sandbox_factory);
  FUSEMounterHelper(const FUSEMounterHelper&) = delete;
  FUSEMounterHelper& operator=(const FUSEMounterHelper&) = delete;
  ~FUSEMounterHelper() override;

 protected:
  const SandboxedProcessFactory* sandbox_factory() const {
    return sandbox_factory_;
  }

  // FUSEMounter overrides:
  std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath& target_path,
      std::vector<std::string> params,
      MountErrorType* error) const final;

  virtual MountErrorType ConfigureSandbox(const std::string& source,
                                          const base::FilePath& target_path,
                                          std::vector<std::string> params,
                                          SandboxedProcess* sandbox) const = 0;

 private:
  const SandboxedProcessFactory* const sandbox_factory_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNTER_H_
