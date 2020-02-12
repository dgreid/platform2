// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/external_task.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>

#include "shill/error.h"
#include "shill/process_manager.h"

namespace shill {

using base::FilePath;
using std::map;
using std::string;
using std::vector;

ExternalTask::ExternalTask(
    ControlInterface* control,
    ProcessManager* process_manager,
    const base::WeakPtr<RpcTaskDelegate>& task_delegate,
    const base::Callback<void(pid_t, int)>& death_callback)
    : control_(control),
      process_manager_(process_manager),
      task_delegate_(task_delegate),
      death_callback_(death_callback),
      pid_(0) {
  CHECK(task_delegate_);
}

ExternalTask::~ExternalTask() {
  ExternalTask::Stop();
}

bool ExternalTask::Start(const FilePath& program,
                         const vector<string>& arguments,
                         const map<string, string>& environment,
                         bool terminate_with_parent,
                         Error* error) {
  CHECK(!pid_);
  CHECK(!rpc_task_);

  // Setup full environment variables.
  auto local_rpc_task = std::make_unique<RpcTask>(control_, this);
  map<string, string> env = local_rpc_task->GetEnvironment();
  env.insert(environment.begin(), environment.end());

  pid_t pid = process_manager_->StartProcess(
      FROM_HERE, program, arguments, env, terminate_with_parent,
      base::Bind(&ExternalTask::OnTaskDied, base::Unretained(this)));

  if (pid < 0) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInternalError,
        string("Unable to spawn: ") + program.value().c_str());
    return false;
  }
  pid_ = pid;
  rpc_task_ = std::move(local_rpc_task);
  return true;
}

bool ExternalTask::StartInMinijail(const FilePath& program,
                                   vector<string>* arguments,
                                   const string user,
                                   const string group,
                                   uint64_t mask,
                                   bool inherit_supplementary_groups,
                                   bool close_nonstd_fds,
                                   Error* error) {
  // Checks will fail if Start or StartInMinijailWithRpcIdentifiers has already
  // been called on this object.
  CHECK(!pid_);
  CHECK(!rpc_task_);

  // Passes the connection identifiers on the command line instead of through
  // environment variables.
  auto local_rpc_task = std::make_unique<RpcTask>(control_, this);
  map<string, string> env = local_rpc_task->GetEnvironment();
  map<string, string>::iterator task_service_variable =
      env.find(kRpcTaskServiceVariable);
  map<string, string>::iterator task_path_variable =
      env.find(kRpcTaskPathVariable);
  // Fails without the necessary environment variables.
  if (task_service_variable == env.end() || task_path_variable == env.end()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          string("Invalid environment variables for: ") +
                              program.value().c_str());
    return false;
  }
  arguments->push_back(base::StringPrintf(
      "--shill_task_service=%s", task_service_variable->second.c_str()));
  arguments->push_back(base::StringPrintf("--shill_task_path=%s",
                                          task_path_variable->second.c_str()));

  pid_t pid = process_manager_->StartProcessInMinijail(
      FROM_HERE, program, *arguments, {}, user, group, mask,
      inherit_supplementary_groups, close_nonstd_fds,
      base::Bind(&ExternalTask::OnTaskDied, base::Unretained(this)));

  if (pid < 0) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInternalError,
                          string("Unable to spawn: ") +
                              program.value().c_str() +
                              string(" in a minijail."));
    return false;
  }
  pid_ = pid;
  rpc_task_ = std::move(local_rpc_task);
  return true;
}

void ExternalTask::Stop() {
  if (pid_) {
    process_manager_->StopProcess(pid_);
    pid_ = 0;
  }
  rpc_task_.reset();
}

void ExternalTask::GetLogin(string* user, string* password) {
  return task_delegate_->GetLogin(user, password);
}

void ExternalTask::Notify(const string& event,
                          const map<string, string>& details) {
  return task_delegate_->Notify(event, details);
}

void ExternalTask::OnTaskDied(int exit_status) {
  CHECK(pid_);
  LOG(INFO) << __func__ << "(" << pid_ << ", " << exit_status << ")";
  pid_t old_pid = pid_;
  pid_ = 0;
  rpc_task_.reset();
  // Since this method has no more non-static member accesses below this call,
  // the death callback is free to destruct this instance.
  death_callback_.Run(old_pid, exit_status);
}

}  // namespace shill
