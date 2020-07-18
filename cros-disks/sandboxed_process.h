// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_SANDBOXED_PROCESS_H_
#define CROS_DISKS_SANDBOXED_PROCESS_H_

#include <sys/types.h>

#include <string>
#include <vector>

#include <base/containers/span.h>
#include <base/files/file.h>

#include "cros-disks/process.h"

struct minijail;

namespace cros_disks {

class SandboxedProcess : public Process {
 public:
  SandboxedProcess();
  ~SandboxedProcess() override;

  // Loads the seccomp filters from |policy_file|. The calling process will be
  // aborted if |policy_file| does not exist, cannot be read or is malformed.
  void LoadSeccompFilterPolicy(const std::string& policy_file);

  // Puts the process to be sandboxed in a new cgroup namespace.
  void NewCgroupNamespace();

  // Puts the process to be sandboxed in a new IPC namespace.
  void NewIpcNamespace();

  // Puts the process to be sandboxed in a new mount namespace.
  void NewMountNamespace();

  // Puts the process to be sandboxed in an existing mount namespace.
  // Can be combined with NewMountNamespace() above: the process will first
  // enter the existing namespace and then unshare a new child namespace.
  void EnterExistingMountNamespace(const std::string& ns_path);

  // Puts the process to be sandboxed in a new network namespace.
  void NewNetworkNamespace();

  // Puts the process to be sandboxed in a new PID namespace.
  void NewPidNamespace();

  // Assuming the process is sandboxed in a new mount namespace, some essential
  // mountpoints like / and /proc are being set up.
  bool SetUpMinimalMounts();

  // Maps a file or a folder into process' mount namespace.
  bool BindMount(const std::string& from,
                 const std::string& to,
                 bool writeable,
                 bool recursive);

  // Mounts source to the specified folder in the new mount namespace.
  bool Mount(const std::string& src,
             const std::string& to,
             const std::string& type,
             const char* data);

  // Makes the process to call pivot_root for an empty /.
  bool EnterPivotRoot();

  // Skips re-marking existing mounts as private.
  void SkipRemountPrivate();

  // Sets the no_new_privs bit.
  void SetNoNewPrivileges();

  // Sets the process capabilities of the process to be sandboxed.
  void SetCapabilities(uint64_t capabilities);

  // Sets the primary group ID of the process to be sandboxed.
  void SetGroupId(gid_t group_id);

  // Sets the user ID of the process to be sandboxed.
  void SetUserId(uid_t user_id);

  // Sets supplementary group IDs of the process to be sandboxed.
  void SetSupplementaryGroupIds(base::span<const gid_t> gids);

  // Adds the minijail to |cgroup|.
  bool AddToCgroup(const std::string& cgroup);

  // Close all open fds on fork.
  void CloseOpenFds();

  // Preserves |file| to still be available in the sandboxed process with the
  // same file descriptor. Only effective if CloseOpenFds has been called.
  bool PreserveFile(const base::File& file);

 protected:
  // Process overrides:
  pid_t StartImpl(base::ScopedFD in_fd,
                  base::ScopedFD out_fd,
                  base::ScopedFD err_fd) override;
  int WaitImpl() override;
  int WaitNonBlockingImpl() override;

 private:
  minijail* jail_;
  bool run_custom_init_ = false;
  base::ScopedFD custom_init_control_fd_;

  DISALLOW_COPY_AND_ASSIGN(SandboxedProcess);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_SANDBOXED_PROCESS_H_
