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
#include "cros-disks/quote.h"
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

OwnerUser ResolveSmbFsUser(const Platform* platform) {
  OwnerUser user;
  PCHECK(platform->GetUserAndGroupId(kUserName, &user.uid, &user.gid));
  return user;
}

}  // namespace

SmbfsHelper::SmbfsHelper(const Platform* platform,
                         brillo::ProcessReaper* process_reaper)
    : FUSEMounterHelper(platform,
                        process_reaper,
                        kType,
                        /* nosymfollow= */ true,
                        &sandbox_factory_),
      sandbox_factory_(platform,
                       SandboxedExecutable{base::FilePath(kHelperTool),
                                           base::FilePath(kSeccompPolicyFile)},
                       ResolveSmbFsUser(platform),
                       /* has_network_access= */ true) {}

SmbfsHelper::~SmbfsHelper() = default;

bool SmbfsHelper::CanMount(const std::string& source,
                           const std::vector<std::string>& params,
                           base::FilePath* suggested_name) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType)
    return false;

  if (uri.path().empty()) {
    *suggested_name = base::FilePath(kType);
  } else {
    *suggested_name = base::FilePath(uri.path());
  }
  return true;
}

MountErrorType SmbfsHelper::ConfigureSandbox(
    const std::string& source,
    const base::FilePath& /*target_path*/,
    std::vector<std::string> /*params*/,
    SandboxedProcess* sandbox) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType || uri.path().empty()) {
    LOG(ERROR) << "Inavlid source " << quote(source);
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  // Bind DBus communication socket and daemon-store into the sandbox.
  if (!sandbox->BindMount(kDbusSocketPath, kDbusSocketPath,
                          /* writable= */ true, /* recursive= */ false)) {
    LOG(ERROR) << "Cannot bind " << quote(kDbusSocketPath);
    return MOUNT_ERROR_INTERNAL;
  }
  // Need to use recursive binding because the daemon-store directory in
  // their cryptohome is bind mounted inside |kDaemonStorePath|.
  // TODO(crbug.com/1054705): Pass the user account hash as a mount option
  // and restrict binding to that specific directory.
  if (!sandbox->BindMount(kDaemonStorePath, kDaemonStorePath,
                          /* writable= */ true, /* recursive= */ true)) {
    LOG(ERROR) << "Cannot bind " << quote(kDaemonStorePath);
    return MOUNT_ERROR_INTERNAL;
  }

  sandbox->AddArgument("-o");
  sandbox->AddArgument(base::JoinString(
      {"uid=1000", "gid=1001", kMojoIdOptionPrefix + uri.path()}, ","));

  return MOUNT_ERROR_NONE;
}

}  // namespace cros_disks
