// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/vpd_process_impl.h"

#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <base/sys_info.h>

#include "login_manager/dbus_error_types.h"

namespace login_manager {

VpdProcessImpl::VpdProcessImpl(SystemUtils* system_utils)
    : system_utils_(system_utils) {
  DCHECK(system_utils_);
}

bool VpdProcessImpl::RunInBackground(const std::vector<std::string>& flags,
                                     const std::vector<int>& values,
                                     const PolicyService::Completion&
                                       completion) {
  DCHECK(flags.size() == values.size());
  subprocess_.reset(new ChildJobInterface::Subprocess(0, system_utils_));

  std::vector<std::string> argv;
  argv.push_back("/usr/sbin/set_binary_flag_vpd");
  for (size_t i = 0; i < flags.size(); i++) {
    argv.push_back(flags[i]);
    argv.push_back(std::to_string(values[i]));
  }

  if (!subprocess_->ForkAndExec(argv, std::vector<std::string>())) {
    // The caller remains responsible for running |completion|.
    return false;
  }

  // |completion_| will be run when the job exits.
  completion_ = completion;
  return true;
}

bool VpdProcessImpl::IsManagedJob(pid_t pid) {
  return subprocess_ && subprocess_->pid() > 0 && subprocess_->pid() == pid;
}

void VpdProcessImpl::RequestJobExit() {
  if (subprocess_ && subprocess_->pid() > 0)
    subprocess_->Kill(SIGTERM);
}

void VpdProcessImpl::EnsureJobExit(base::TimeDelta timeout) {
  if (subprocess_) {
    if (subprocess_->pid() < 0)
      return;
    if (!system_utils_->ProcessGroupIsGone(subprocess_->pid(), timeout)) {
      subprocess_->KillEverything(SIGABRT);
      DLOG(INFO) << "Child process was killed.";
    }
  }
}

void VpdProcessImpl::HandleExit(const siginfo_t& info) {
  if (completion_.is_null()) {
    LOG(ERROR) << "No completion provided.";
    return;
  }

  // We have to notify Chrome that the process has finished.
  if (info.si_status == 0) {
    completion_.Run(PolicyService::Error());
  } else {
    LOG(ERROR) << "Failed to update VPD, code = " << info.si_status;
    // TODO(igorcov): Remove the exception when crbug/653814 is fixed.
    std::string board_name = base::ToLowerASCII(
        base::SysInfo::GetLsbReleaseBoard());
    if (board_name == "parrot" || board_name == "glimmer") {
      completion_.Run(PolicyService::Error());
    } else {
      completion_.Run(PolicyService::Error(dbus_error::kVpdUpdateFailed,
                     "Failed to update VPD"));
    }
  }

  // Reset the completion to ensure we won't call it again.
  completion_.Reset();
}

}  // namespace login_manager
