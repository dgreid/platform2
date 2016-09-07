// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_CONTAINER_MANAGER_INTERFACE_H_
#define LOGIN_MANAGER_CONTAINER_MANAGER_INTERFACE_H_

#include <sys/types.h>

#include <string>

#include <base/callback_forward.h>
#include <base/files/file_path.h>

#include "login_manager/job_manager.h"

namespace login_manager {

// Provides methods for running and stopping containers.
//
// Containers can only be run from the verified rootfs.
class ContainerManagerInterface : public JobManagerInterface {
 public:
  // |clean| is true if the container was shut down through RequestJobExit.
  using ExitCallback = base::Callback<void(pid_t, bool clean)>;

  virtual ~ContainerManagerInterface() {}

  // Starts the container. Returns true on success.
  // If successful, |exit_callback| will be notified when the process exits.
  virtual bool StartContainer(const ExitCallback& exit_callback) = 0;

  // Gets the path of the rootfs of the container.
  virtual bool GetRootFsPath(base::FilePath* path_out) const = 0;

  // Gets the process ID of the container.
  virtual bool GetContainerPID(pid_t* pid_out) const = 0;

  // Prioritizes the container by reverting cgroups settings back to default.
  virtual bool PrioritizeContainer() = 0;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_CONTAINER_MANAGER_INTERFACE_H_
