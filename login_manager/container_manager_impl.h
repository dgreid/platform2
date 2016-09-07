// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_
#define LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_

#include "login_manager/container_manager_interface.h"

#include <stdlib.h>

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>

#include <libcontainer/libcontainer.h>

namespace login_manager {

class SystemUtils;

// Manages containers running in a session.
// Handles parsing of config.json and runtime.json to configure the container.
class ContainerManagerImpl : public ContainerManagerInterface {
 public:
  ContainerManagerImpl(SystemUtils* system_utils,
                       const base::FilePath& containers_directory,
                       const std::string& name);
  ~ContainerManagerImpl() override;

  // JobManagerInterface:
  bool IsManagedJob(pid_t pid) override;
  void HandleExit(const siginfo_t& status) override;
  void RequestJobExit() override;
  void EnsureJobExit(base::TimeDelta timeout) override;

  // ContainerManagerInterface:
  bool StartContainer(const ExitCallback& exit_callback) override;
  bool GetRootFsPath(base::FilePath* path_out) const override;
  bool GetContainerPID(pid_t* pid_out) const override;
  bool PrioritizeContainer() override;

  using ContainerPtr = std::unique_ptr<container, decltype(&container_destroy)>;

 protected:
  // This will be called from RequestJobExit(). If this method returns false,
  // the container will be forcibly terminated.
  virtual bool RequestTermination();

  // This will be called once the container has been considered to be terminated
  // but before |exit_callback_| is run. This will allow subclasses to measure
  // shutdown timing. |clean| is true only if the container was cleanly shut
  // down.
  virtual void OnContainerStopped(bool clean);

 private:
  // Frees any resources used by the container.
  void CleanUpContainer(pid_t pid);

  // Owned by the caller.
  SystemUtils* const system_utils_;

  // Directory that holds the container config files.
  const base::FilePath container_directory_;

  // Name of the container.
  const std::string name_;

  // Currently running container.
  ContainerPtr container_;

  // Callback that will get invoked when the process exits.
  ExitCallback exit_callback_;

  // True if RequestJobExit was called before the container process exits.
  bool clean_exit_;

  DISALLOW_COPY_AND_ASSIGN(ContainerManagerImpl);
};

}  // namespace login_manager
#endif  // LOGIN_MANAGER_CONTAINER_MANAGER_IMPL_H_
