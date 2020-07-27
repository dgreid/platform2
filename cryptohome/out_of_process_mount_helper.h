// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// OutOfProcessMountHelper objects carry out mount(2) and unmount(2) operations
// for a single cryptohome mount, but do so out-of-process.

#ifndef CRYPTOHOME_OUT_OF_PROCESS_MOUNT_HELPER_H_
#define CRYPTOHOME_OUT_OF_PROCESS_MOUNT_HELPER_H_

#include <sys/types.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <brillo/process/process.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>

#include "cryptohome/mount_constants.h"
#include "cryptohome/mount_helper.h"
#include "cryptohome/mount_namespace.h"
#include "cryptohome/platform.h"

using base::FilePath;

namespace cryptohome {

// Forward declare classes in
// cryptohome/namespace_mounter/namespace_mounter_ipc.proto
class OutOfProcessMountRequest;
class OutOfProcessMountResponse;

class OutOfProcessMountHelper : public MountHelperInterface {
 public:
  OutOfProcessMountHelper(const brillo::SecureBlob& system_salt,
                          std::unique_ptr<MountNamespace> chrome_mnt_ns,
                          bool legacy_home,
                          Platform* platform)
      : system_salt_(system_salt),
        chrome_mnt_ns_(std::move(chrome_mnt_ns)),
        legacy_home_(legacy_home),
        platform_(platform),
        username_(),
        write_to_helper_(-1) {}
  ~OutOfProcessMountHelper() = default;

  // Carries out dircrypto mount(2) operations for an ephemeral cryptohome,
  // but does so out of process.
  bool PerformEphemeralMount(const std::string& username) override;

  // Tears down an ephemeral cryptohome mount by terminating the out-of-process
  // helper.
  bool TearDownEphemeralMount() override;

  // Returns whether an ephemeral mount operation can be performed.
  bool CanPerformEphemeralMount() const override;

  // Returns whether a mount operation has been performed.
  bool MountPerformed() const override;

  // Returns whether |path| is the destination of an existing mount.
  bool IsPathMounted(const base::FilePath& path) const override;

  // Carries out dircrypto mount(2) operations for a regular cryptohome.
  bool PerformMount(const Options& mount_opts,
                    const std::string& username,
                    const std::string& fek_signature,
                    const std::string& fnek_signature,
                    bool is_pristine,
                    MountError* error) override;

 private:
  // Launches an out-of-process helper, sends |request|, and waits until it
  // receives |response|. The timeout for receiving |response| is
  // |kOutOfProcessHelperMountTimeout| seconds.
  bool LaunchOutOfProcessHelper(const OutOfProcessMountRequest& request,
                                OutOfProcessMountResponse* response);

  // Kills the out-of-process helper if it's still running, and Reset()s the
  // Process instance to close all pipe file descriptors.
  void KillOutOfProcessHelperIfNecessary();

  // Stores the global system salt.
  brillo::SecureBlob system_salt_;

  // If populated, mount namespace where to perform the mount.
  std::unique_ptr<MountNamespace> chrome_mnt_ns_;

  // Whether to make the legacy home directory (/home/chronos/user) available.
  bool legacy_home_;

  Platform* platform_;  // Un-owned.

  // Username the mount belongs to, if a mount has been performed.
  // Empty otherwise.
  std::string username_;

  // Tracks the helper process.
  std::unique_ptr<brillo::Process> helper_process_;

  // Pipe used to communicate with the helper process.
  // This file descriptor is owned by |helper_process_|, so it's not
  // scoped.
  int write_to_helper_;

  // Set of mounts returned by the helper.
  std::set<std::string> mounted_paths_;

  DISALLOW_COPY_AND_ASSIGN(OutOfProcessMountHelper);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_OUT_OF_PROCESS_MOUNT_HELPER_H_
