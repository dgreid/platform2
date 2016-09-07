// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/fake_container_manager.h"

namespace login_manager {

FakeContainerManager::FakeContainerManager(pid_t pid) : pid_(pid) {}

bool FakeContainerManager::IsManagedJob(pid_t pid) {
  return running_ && pid_ == pid;
}

void FakeContainerManager::HandleExit(const siginfo_t& status) {}

void FakeContainerManager::RequestJobExit() {
  LOG_IF(FATAL, !running_) << "Trying to stop an already stopped container";
  running_ = false;
  exit_callback_.Run(pid_, true);
}

void FakeContainerManager::EnsureJobExit(base::TimeDelta timeout) {}

bool FakeContainerManager::StartContainer(const ExitCallback& exit_callback) {
  LOG_IF(FATAL, running_) << "Trying to start an already started container";
  exit_callback_ = exit_callback;
  running_ = true;
  return true;
}

bool FakeContainerManager::GetRootFsPath(base::FilePath* path_out) const {
  if (!running_)
    return false;
  *path_out = base::FilePath();
  return true;
}

bool FakeContainerManager::GetContainerPID(pid_t* pid_out) const {
  if (!running_)
    return false;
  *pid_out = pid_;
  return true;
}

bool FakeContainerManager::PrioritizeContainer() {
  return true;
}

void FakeContainerManager::SimulateCrash() {
  LOG_IF(FATAL, !running_) << "Trying to crash an already stopped container";
  running_ = false;
  exit_callback_.Run(pid_, false);
}

}  // namespace login_manager
