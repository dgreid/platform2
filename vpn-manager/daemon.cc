// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vpn-manager/daemon.h"

#include <signal.h>

#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/process.h>

using ::brillo::Process;
using ::brillo::ProcessImpl;

namespace vpn_manager {

// static
const int Daemon::kTerminationTimeoutSeconds = 2;

Daemon::Daemon(const std::string& pid_file) : pid_file_(pid_file) {}

Daemon::~Daemon() {
  ClearProcess();
}

void Daemon::ClearProcess() {
  SetProcess(nullptr);
}

Process* Daemon::CreateProcess() {
  return SetProcess(std::make_unique<ProcessImpl>());
}

bool Daemon::FindProcess() {
  if (!base::PathExists(base::FilePath(pid_file_)))
    return false;

  std::unique_ptr<brillo::Process> process(new ProcessImpl);
  process->ResetPidByFile(pid_file_);
  if (!Process::ProcessExists(process->pid())) {
    process->Release();
    return false;
  }

  SetProcess(std::move(process));
  return true;
}

bool Daemon::IsRunning() {
  return process_ && process_->pid() != 0 &&
         Process::ProcessExists(process_->pid());
}

pid_t Daemon::GetPid() const {
  return process_ ? process_->pid() : 0;
}

Process* Daemon::SetProcess(std::unique_ptr<Process> process) {
  if (process_) {
    // If we are re-assigning the same pid, do not terminate the process.
    // Otherwise, we should kill the previous process if it is still running.
    if (process && process_->pid() == process->pid())
      process_->Release();
    else if (IsRunning())
      process_->Kill(SIGKILL, kTerminationTimeoutSeconds);
  }

  process_ = std::move(process);
  return process_.get();
}

bool Daemon::Terminate() {
  bool result =
      !IsRunning() || process_->Kill(SIGTERM, kTerminationTimeoutSeconds);
  ClearProcess();  // This will send a SIGKILL if we failed above.
  base::DeleteFile(base::FilePath(pid_file_), false);
  return result;
}

}  // namespace vpn_manager
