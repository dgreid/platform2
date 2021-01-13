// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_ARCHIVE_MOUNTER_H_
#define CROS_DISKS_ARCHIVE_MOUNTER_H_

#include <memory>
#include <string>
#include <vector>

#include "cros-disks/fuse_mounter.h"

namespace cros_disks {

class Metrics;

// An implementation of FUSEMounter tailored for mounting archives.
class ArchiveMounter : public FUSEMounter {
 public:
  static constexpr char kChromeNamespace[] = "/run/namespaces/mnt_chrome";

  ArchiveMounter(const Platform* platform,
                 brillo::ProcessReaper* process_reaper,
                 std::string archive_type,
                 Metrics* metrics,
                 std::string metrics_name,
                 std::vector<int> password_needed_exit_codes,
                 std::unique_ptr<SandboxedProcessFactory> sandbox_factory);
  ArchiveMounter(const ArchiveMounter&) = delete;
  ArchiveMounter& operator=(const ArchiveMounter&) = delete;

  ~ArchiveMounter() override;

  bool CanMount(const std::string& source,
                const std::vector<std::string>& params,
                base::FilePath* suggested_dir_name) const override;

  OwnerUser GetDaemonUser() const;

 protected:
  // FUSEMounter overrides:
  MountErrorType InterpretReturnCode(int return_code) const override;

  std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath& target_path,
      std::vector<std::string> params,
      MountErrorType* error) const final;

  virtual MountErrorType FormatInvocationCommand(
      const base::FilePath& archive,
      std::vector<std::string> params,
      SandboxedProcess* sandbox) const;

 private:
  const std::string archive_type_;
  const std::string extension_;
  Metrics* const metrics_;
  const std::string metrics_name_;
  const std::vector<int> password_needed_exit_codes_;
  const std::unique_ptr<SandboxedProcessFactory> sandbox_factory_;

  friend class ArchiveMounterTest;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_ARCHIVE_MOUNTER_H_
