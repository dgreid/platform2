// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/sandboxed_process.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <base/strings/stringprintf.h>

namespace {
const size_t kMaxWaitAttempts = 3;
const unsigned int kDelayUSec = 1000;
}

namespace debugd {

const char *SandboxedProcess::kDefaultUser = "debugd";
const char *SandboxedProcess::kDefaultGroup = "debugd";

SandboxedProcess::SandboxedProcess()
    : sandboxing_(true),
      access_root_mount_ns_(false),
      user_(kDefaultUser),
      group_(kDefaultGroup) {
}

SandboxedProcess::~SandboxedProcess() {
  for (const auto& fd : bound_fds_)
    close(fd);
}

// static
bool SandboxedProcess::GetHelperPath(const std::string& relative_path,
                                     std::string* full_path) {
  // This environment variable controls the root directory for debugd helpers,
  // which lets people develop helpers even when verified boot is on.
  const char* helpers_dir = getenv("DEBUGD_HELPERS");
  std::string path = base::StringPrintf(
      "%s/%s",
      helpers_dir ? helpers_dir : "/usr/libexec/debugd/helpers",
      relative_path.c_str());

  if (path.length() > PATH_MAX)
    return false;

  *full_path = path;
  return true;
}

bool SandboxedProcess::Init() {
  const char *kMiniJail = "/sbin/minijail0";

  AddArg(kMiniJail);
  // Enter a new mount namespace. This is done for every process to avoid
  // affecting the original mount namespace.
  AddArg("-v");

  if (sandboxing_) {
    if (user_.empty() || group_.empty())
      return false;

    if (user_ != "root") {
      AddArg("-u");
      AddArg(user_);
    }
    if (group_ != "root") {
      AddArg("-g");
      AddArg(group_);
    }
  }

  if (access_root_mount_ns_) {
    // Enter root mount namespace.
    AddStringOption("-V", "/proc/1/ns/mnt");
  }

  AddArg("--");

  return true;
}

void SandboxedProcess::BindFd(int parent_fd, int child_fd) {
  ProcessImpl::BindFd(parent_fd, child_fd);
  bound_fds_.push_back(parent_fd);
}

void SandboxedProcess::DisableSandbox() {
  sandboxing_ = false;
}

void SandboxedProcess::SandboxAs(const std::string& user,
                                 const std::string& group) {
  sandboxing_ = true;
  user_ = user;
  group_ = group;
}

void SandboxedProcess::AllowAccessRootMountNamespace() {
  access_root_mount_ns_ = true;
}

bool SandboxedProcess::KillProcessGroup() {
  pid_t minijail_pid = pid();
  if (minijail_pid == 0) {
    LOG(ERROR) << "Process is not running";
    return false;
  }

  // Minijail sets its process group ID equal to its PID,
  // so we can use pid() as PGID. Check that's still the case.
  pid_t pgid = getpgid(minijail_pid);
  if (pgid < 0) {
    PLOG(ERROR) << "getpgid(minijail_pid) failed";
    return false;
  }
  if (pgid != minijail_pid) {
    LOG(ERROR) << "Minijail PGID " << pgid << " is different from PID "
               << minijail_pid;
    return false;
  }

  // kill(-pgid) kills every process with process group ID |pgid|.
  if (kill(-pgid, SIGKILL) < 0) {
    PLOG(ERROR) << "kill(-pgid, SIGKILL) failed";
    return false;
  }

  // If kill(2) succeeded, we release the PID.
  UpdatePid(0);

  // We only expect to reap one process, the Minijail process.
  // If the jailed process dies first, Minijail or init will reap it.
  // If the Minijail process dies first, we will reap it. The jailed process
  // will then be reaped by init.
  for (size_t attempt = 0; attempt < kMaxWaitAttempts; ++attempt) {
    int status = 0;
    // waitpid(-pgid) waits for any child process with process group ID |pgid|.
    pid_t waited = waitpid(-pgid, &status, WNOHANG);
    int saved_errno = errno;

    if (waited < 0) {
      if (saved_errno == ECHILD) {
        // Processes with PGID |pgid| don't exist, so we're done.
        return true;
      }
      PLOG(ERROR) << "waitpid(-pgid) failed";
      return false;
    }

    if (waited > 0) {
      if (waited != minijail_pid) {
        LOG(WARNING) << "Expecting PID " << minijail_pid << ", got PID "
                     << waited;
      }
      return true;
    }

    usleep(kDelayUSec);
  }

  LOG(WARNING) << "Process " << minijail_pid << " did not terminate";
  return false;
}

}  // namespace debugd
