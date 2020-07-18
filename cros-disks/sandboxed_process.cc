// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sandboxed_process.h"

#include <utility>

#include <stdlib.h>

#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/safe_strerror.h>
#include <chromeos/libminijail.h>

#include "cros-disks/mount_options.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_init.h"

namespace cros_disks {
namespace {

int Exec(char* const args[], char* const env[]) {
  const char* const path = args[0];
  execve(path, args, env);
  const int ret =
      (errno == ENOENT ? MINIJAIL_ERR_NO_COMMAND : MINIJAIL_ERR_NO_ACCESS);
  PLOG(ERROR) << "Cannot exec " << quote(path);
  return ret;
}

}  // namespace

SandboxedProcess::SandboxedProcess() : jail_(minijail_new()) {
  CHECK(jail_) << "Failed to create a process jail";
}

SandboxedProcess::~SandboxedProcess() {
  minijail_destroy(jail_);
}

void SandboxedProcess::LoadSeccompFilterPolicy(const std::string& policy_file) {
  minijail_parse_seccomp_filters(jail_, policy_file.c_str());
  minijail_use_seccomp_filter(jail_);
}

void SandboxedProcess::NewCgroupNamespace() {
  minijail_namespace_cgroups(jail_);
}

void SandboxedProcess::NewIpcNamespace() {
  minijail_namespace_ipc(jail_);
}

void SandboxedProcess::NewMountNamespace() {
  minijail_namespace_vfs(jail_);
}

void SandboxedProcess::EnterExistingMountNamespace(const std::string& ns_path) {
  minijail_namespace_enter_vfs(jail_, ns_path.c_str());
}

void SandboxedProcess::NewPidNamespace() {
  minijail_namespace_pids(jail_);
  minijail_run_as_init(jail_);
  minijail_reset_signal_mask(jail_);
  minijail_reset_signal_handlers(jail_);
  run_custom_init_ = true;
}

bool SandboxedProcess::SetUpMinimalMounts() {
  if (minijail_bind(jail_, "/", "/", 0))
    return false;
  if (minijail_bind(jail_, "/proc", "/proc", 0))
    return false;
  minijail_remount_proc_readonly(jail_);
  minijail_mount_tmp(jail_);

  // Create a minimal /dev with a very restricted set of device nodes.
  minijail_mount_dev(jail_);
  if (minijail_bind(jail_, "/dev/log", "/dev/log", 0))
    return false;
  return true;
}

bool SandboxedProcess::BindMount(const std::string& from,
                                 const std::string& to,
                                 bool writeable,
                                 bool recursive) {
  MountOptions::Flags flags = MS_BIND;
  if (!writeable) {
    flags |= MS_RDONLY;
  }
  if (recursive) {
    flags |= MS_REC;
  }
  return minijail_mount(jail_, from.c_str(), to.c_str(), "", flags) == 0;
}

bool SandboxedProcess::Mount(const std::string& src,
                             const std::string& to,
                             const std::string& type,
                             const char* data) {
  return minijail_mount_with_data(jail_, src.c_str(), to.c_str(), type.c_str(),
                                  0, data) == 0;
}

bool SandboxedProcess::EnterPivotRoot() {
  return minijail_enter_pivot_root(jail_, "/mnt/empty") == 0;
}

void SandboxedProcess::NewNetworkNamespace() {
  minijail_namespace_net(jail_);
}

void SandboxedProcess::SkipRemountPrivate() {
  minijail_skip_remount_private(jail_);
}

void SandboxedProcess::SetNoNewPrivileges() {
  minijail_no_new_privs(jail_);
}

void SandboxedProcess::SetCapabilities(uint64_t capabilities) {
  minijail_use_caps(jail_, capabilities);
}

void SandboxedProcess::SetGroupId(gid_t group_id) {
  minijail_change_gid(jail_, group_id);
}

void SandboxedProcess::SetUserId(uid_t user_id) {
  minijail_change_uid(jail_, user_id);
}

void SandboxedProcess::SetSupplementaryGroupIds(base::span<const gid_t> gids) {
  minijail_set_supplementary_gids(jail_, gids.size(), gids.data());
}

bool SandboxedProcess::AddToCgroup(const std::string& cgroup) {
  return minijail_add_to_cgroup(jail_, cgroup.c_str()) == 0;
}

void SandboxedProcess::CloseOpenFds() {
  minijail_close_open_fds(jail_);
}

bool SandboxedProcess::PreserveFile(const base::File& file) {
  return minijail_preserve_fd(jail_, file.GetPlatformFile(),
                              file.GetPlatformFile()) == 0;
}

pid_t SandboxedProcess::StartImpl(base::ScopedFD in_fd,
                                  base::ScopedFD out_fd,
                                  base::ScopedFD err_fd) {
  char* const* const args = GetArguments();
  DCHECK(args && args[0]);
  char* const* const env = GetEnvironment();
  DCHECK(env);

  pid_t child_pid = kInvalidProcessId;

  if (!run_custom_init_) {
    minijail_preserve_fd(jail_, in_fd.get(), STDIN_FILENO);
    minijail_preserve_fd(jail_, out_fd.get(), STDOUT_FILENO);
    minijail_preserve_fd(jail_, err_fd.get(), STDERR_FILENO);

    const int ret = minijail_run_env_pid_pipes(
        jail_, args[0], args, env, &child_pid, nullptr, nullptr, nullptr);
    if (ret < 0) {
      LOG(ERROR) << "Cannot start minijail process: "
                 << base::safe_strerror(-ret);
      return kInvalidProcessId;
    }
  } else {
    SandboxedInit init(std::move(in_fd), std::move(out_fd), std::move(err_fd),
                       SubprocessPipe::Open(SubprocessPipe::kChildToParent,
                                            &custom_init_control_fd_));

    // Create child process.
    child_pid = minijail_fork(jail_);
    if (child_pid < 0) {
      LOG(ERROR) << "Cannot run minijail_fork: "
                 << base::safe_strerror(-child_pid);
      return kInvalidProcessId;
    }

    if (child_pid == 0) {
      // In child process.
      init.RunInsideSandboxNoReturn(base::BindOnce(Exec, args, env));
      NOTREACHED();
    } else {
      // In parent process.
      CHECK(base::SetNonBlocking(custom_init_control_fd_.get()));
    }
  }

  return child_pid;
}

int SandboxedProcess::WaitImpl() {
  while (true) {
    const int status = minijail_wait(jail_);
    if (status >= 0) {
      return status;
    }

    const int err = -status;
    if (err != EINTR) {
      LOG(ERROR) << "Cannot wait for process " << pid() << ": "
                 << base::safe_strerror(err);
      return MINIJAIL_ERR_INIT;
    }
  }
}

int SandboxedProcess::WaitNonBlockingImpl() {
  int exit_code;

  if (run_custom_init_ &&
      SandboxedInit::PollLauncherStatus(&custom_init_control_fd_, &exit_code)) {
    return exit_code;
  }

  // TODO(chromium:971667) Use Minijail's non-blocking wait once it exists.
  int wstatus;
  const pid_t child_pid = pid();
  const int ret = waitpid(child_pid, &wstatus, WNOHANG);
  if (ret < 0) {
    PLOG(ERROR) << "Cannot wait for process " << child_pid;
    return MINIJAIL_ERR_INIT;
  }

  if (ret == 0) {
    // Process is still running.
    return -1;
  }

  return SandboxedInit::WStatusToStatus(wstatus);
}

}  // namespace cros_disks
