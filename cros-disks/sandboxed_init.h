// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_SANDBOXED_INIT_H_
#define CROS_DISKS_SANDBOXED_INIT_H_

#include <memory>
#include <string>
#include <vector>

#include <sys/types.h>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>

namespace cros_disks {

// Anonymous pipe to establish communication between a parent process and a
// child process.
struct SubprocessPipe {
  base::ScopedFD child_fd, parent_fd;

  // Direction of communication.
  enum Direction { kChildToParent, kParentToChild };

  // Creates an open pipe. Sets FD_CLOEXEC on parent_fd. Dies in case of error.
  explicit SubprocessPipe(Direction direction);

  // Opens a pipe to communicate with a child process. Returns the end of the
  // pipe that is used by the child process. Stores the end of the pipe that is
  // kept by the parent process in *parent_fd and flags it with FD_CLOEXEC. Dies
  // in case of error.
  static base::ScopedFD Open(Direction direction, base::ScopedFD* parent_fd);
};

// To run daemons in a PID namespace under minijail we need to provide
// an "init" process for the sandbox. As we rely on return code of the
// launcher of the daemonized process we must send it through a side
// channel back to the caller without waiting to the whole PID namespace
// to terminate.
class SandboxedInit {
 public:
  SandboxedInit(base::ScopedFD in_fd,
                base::ScopedFD out_fd,
                base::ScopedFD err_fd,
                base::ScopedFD ctrl_fd);
  SandboxedInit(const SandboxedInit&) = delete;
  SandboxedInit& operator=(const SandboxedInit&) = delete;

  ~SandboxedInit();

  // To be run inside the jail. Never returns.
  [[noreturn]] void RunInsideSandboxNoReturn(
      base::OnceCallback<int()> launcher);

  static bool PollLauncherStatus(base::ScopedFD* ctrl_fd, int* exit_code);

  static int WStatusToStatus(int wstatus);

 private:
  int RunInitLoop(pid_t root_pid, base::ScopedFD ctrl_fd);
  pid_t StartLauncher(base::OnceCallback<int()> launcher);

  base::ScopedFD in_fd_, out_fd_, err_fd_, ctrl_fd_;
};

}  // namespace cros_disks

#endif  // CROS_DISKS_SANDBOXED_INIT_H_
