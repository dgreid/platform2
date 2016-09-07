// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/container_manager_impl.h"

#include <errno.h>
#include <stdint.h>
#include <sys/mount.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/posix/safe_strerror.h>
#include <base/strings/stringprintf.h>

#include <libcontainer/libcontainer.h>

#include "login_manager/container_config_parser.h"
#include "login_manager/system_utils.h"

namespace login_manager {

namespace {

const char kContainerRunPath[] = "/run/containers";
const char kSessionManagerCgroup[] = "session_manager_containers";

std::string libcontainer_strerror(int err) {
  if (err < 0) {
    // Negative values come from -errno. Change the sign back for
    // safe_strerror().
    return base::safe_strerror(-err);
  } else {
    // Otherwise, it might have been a libminijail error code.
    return base::StringPrintf("libminijail error code %d", err);
  }
}

ContainerManagerImpl::ContainerPtr CreateContainer(const std::string& name) {
  return ContainerManagerImpl::ContainerPtr(
      container_new(name.c_str(), kContainerRunPath), &container_destroy);
}

}  // anonymous namespace

ContainerManagerImpl::ContainerManagerImpl(
    SystemUtils* system_utils,
    const base::FilePath& containers_directory,
    const std::string& name)
    : system_utils_(system_utils),
      container_directory_(containers_directory.Append(name)),
      name_(name),
      container_(nullptr, &container_destroy),
      clean_exit_(false) {
  DCHECK(system_utils_);
}

ContainerManagerImpl::~ContainerManagerImpl() {}

bool ContainerManagerImpl::IsManagedJob(pid_t pid) {
  pid_t container_pid;
  return GetContainerPID(&container_pid) && container_pid == pid;
}

void ContainerManagerImpl::HandleExit(const siginfo_t& status) {
  pid_t pid;
  if (!GetContainerPID(&pid)) {
    LOG(ERROR) << "Container " << name_ << " unexpected exit.";
    return;
  }

  CleanUpContainer(pid);
}

void ContainerManagerImpl::RequestJobExit() {
  pid_t pid;
  if (!GetContainerPID(&pid))
    return;

  // If HandleExit() is called after this point, it is considered clean.
  clean_exit_ = true;

  if (!RequestTermination()) {
    LOG(INFO) << "Killing off container " << name_;
    int rc = container_kill(container_.get());
    if (rc != 0) {
      LOG(ERROR) << "Failed to kill container " << name_ << ": "
                 << libcontainer_strerror(rc);
    }
  }
}

void ContainerManagerImpl::EnsureJobExit(base::TimeDelta timeout) {
  if (!container_)
    return;

  pid_t pid;
  if (!GetContainerPID(&pid))
    return;

  if (!system_utils_->ProcessGroupIsGone(pid, timeout)) {
    LOG(INFO) << "Killing off container " << name_;
    int rc = container_kill(container_.get());
    if (rc != 0) {
      LOG(ERROR) << "Failed to kill container " << name_ << ": "
                 << libcontainer_strerror(rc);
    }
  }
  CleanUpContainer(pid);
}

bool ContainerManagerImpl::StartContainer(const ExitCallback& exit_callback) {
  // TODO(lhchavez): Make logging less verbose once we're comfortable that
  // everything works correctly. See b/29266253.
  LOG(INFO) << "Starting container " << name_;
  if (container_) {
    LOG(ERROR) << "Container " << name_ << " already running";
    return false;
  }

  std::string config_json_data;
  if (!base::ReadFileToString(container_directory_.Append("config.json"),
                              &config_json_data)) {
    PLOG(ERROR) << "Fail to read config for " << name_;
    return false;
  }

  std::string runtime_json_data;
  if (!base::ReadFileToString(container_directory_.Append("runtime.json"),
                              &runtime_json_data)) {
    LOG(ERROR) << "Fail to read runtime config for " << name_;
    return false;
  }

  ContainerConfigPtr config(container_config_create(),
                            &container_config_destroy);
  if (!ParseContainerConfig(config_json_data, runtime_json_data, name_,
                            kSessionManagerCgroup, container_directory_,
                            &config)) {
    LOG(ERROR) << "Failed to parse container configuration for " << name_;
    return false;
  }

  ContainerPtr new_container = CreateContainer(name_);
  if (!new_container) {
    LOG(ERROR) << "Failed to create the new container named " << name_;
    return false;
  }

  int rc = container_start(new_container.get(), config.get());
  if (rc != 0) {
    LOG(ERROR) << "Failed to start container " << name_ << ": "
               << libcontainer_strerror(rc);
    return false;
  }

  container_ = std::move(new_container);
  exit_callback_ = exit_callback;
  clean_exit_ = false;

  return true;
}

bool ContainerManagerImpl::GetRootFsPath(base::FilePath* path_out) const {
  if (!container_)
    return false;
  *path_out = base::FilePath(container_root(container_.get()));
  return true;
}

bool ContainerManagerImpl::GetContainerPID(pid_t* pid_out) const {
  if (!container_)
    return false;
  *pid_out = container_pid(container_.get());
  return true;
}

bool ContainerManagerImpl::PrioritizeContainer() {
  // TODO(yusukes|dgreid): Implement this and change the return value to true.
  return false;
}

bool ContainerManagerImpl::RequestTermination() {
  return false;
}

void ContainerManagerImpl::OnContainerStopped(bool clean) {}

void ContainerManagerImpl::CleanUpContainer(pid_t pid) {
  if (!container_)
    return;

  LOG(INFO) << "Cleaning up container " << name_;
  int rc = container_wait(container_.get());
  if (rc != 0) {
    LOG(ERROR) << "Failed to clean up container " << name_ << ": "
               << libcontainer_strerror(rc);
  }

  // Save callbacks until after everything has been cleaned up.
  ExitCallback exit_callback;
  std::swap(exit_callback_, exit_callback);

  container_.reset();
  exit_callback_.Reset();

  OnContainerStopped(clean_exit_);

  if (!exit_callback.is_null())
    exit_callback.Run(pid, clean_exit_);
}

}  // namespace login_manager
