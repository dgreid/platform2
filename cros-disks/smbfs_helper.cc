// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/smbfs_helper.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

const char kUserName[] = "fuse-smbfs";
const char kHelperTool[] = "/usr/sbin/smbfs";
const char kType[] = "smbfs";
const char kSeccompPolicyFile[] = "/usr/share/policy/smbfs-seccomp.policy";

const char kMojoIdOptionPrefix[] = "mojo_id=";
const char kDbusSocketPath[] = "/run/dbus";
const char kDaemonStorePath[] = "/run/daemon-store/smbfs";

class SmbfsMounter : public FUSEMounterLegacy {
 public:
  SmbfsMounter(std::string filesystem_type,
               MountOptions mount_options,
               const Platform* platform,
               brillo::ProcessReaper* process_reaper,
               std::string mount_program,
               std::string mount_user,
               std::string seccomp_policy,
               BindPaths bind_paths)
      : FUSEMounterLegacy({.bind_paths = std::move(bind_paths),
                           .filesystem_type = std::move(filesystem_type),
                           .mount_options = std::move(mount_options),
                           .mount_program = std::move(mount_program),
                           .mount_user = std::move(mount_user),
                           .network_access = true,
                           .platform = platform,
                           .process_reaper = process_reaper,
                           .seccomp_policy = seccomp_policy}) {}

  // FUSEMounterLegacy overrides:
  std::unique_ptr<SandboxedProcess> PrepareSandbox(
      const std::string& source,
      const base::FilePath& target_path,
      std::vector<std::string> params,
      MountErrorType* error) const override {
    return FUSEMounterLegacy::PrepareSandbox("", target_path, params, error);
  }
};

}  // namespace

SmbfsHelper::SmbfsHelper(const Platform* platform,
                         brillo::ProcessReaper* process_reaper)
    : FUSEHelper(kType,
                 platform,
                 process_reaper,
                 base::FilePath(kHelperTool),
                 kUserName) {}

SmbfsHelper::~SmbfsHelper() = default;

std::unique_ptr<FUSEMounter> SmbfsHelper::CreateMounter(
    const base::FilePath& working_dir,
    const Uri& source,
    const base::FilePath& target_path,
    const std::vector<std::string>& options) const {
  const std::string& mojo_id = source.path();

  // Enforced by FUSEHelper::CanMount().
  DCHECK(!mojo_id.empty());

  uid_t files_uid;
  gid_t files_gid;
  if (!platform()->GetUserAndGroupId(kFilesUser, &files_uid, nullptr) ||
      !platform()->GetGroupId(kFilesGroup, &files_gid)) {
    return nullptr;
  }

  MountOptions mount_options;
  mount_options.EnforceOption(kMojoIdOptionPrefix + mojo_id);
  mount_options.Initialize(options, true, base::NumberToString(files_uid),
                           base::NumberToString(files_gid));

  // Bind DBus communication socket and daemon-store into the sandbox.
  FUSEMounterLegacy::BindPaths paths = {
      {kDbusSocketPath, true},
      // Need to use recursive binding because the daemon-store directory in
      // their cryptohome is bind mounted inside |kDaemonStorePath|.
      // TODO(crbug.com/1054705): Pass the user account hash as a mount option
      // and restrict binding to that specific directory.
      {kDaemonStorePath, true /* writable */, true /* recursive */},
  };

  return std::make_unique<SmbfsMounter>(
      type(), mount_options, platform(), process_reaper(),
      program_path().value(), user(), kSeccompPolicyFile, paths);
}

}  // namespace cros_disks
