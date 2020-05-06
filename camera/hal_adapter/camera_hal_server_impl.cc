/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_hal_server_impl.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/threading/thread_task_runner_handle.h>

#include "common/utils/camera_hal_enumerator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"
#include "cros-camera/utils/camera_config.h"
#include "hal_adapter/camera_hal_test_adapter.h"
#include "hal_adapter/camera_trace_event.h"

namespace cros {

CameraHalServerImpl::CameraHalServerImpl()
    : main_task_runner_(base::ThreadTaskRunnerHandle::Get()), binding_(this) {
  VLOGF_ENTER();
}

CameraHalServerImpl::~CameraHalServerImpl() {
  VLOGF_ENTER();
}

bool CameraHalServerImpl::Start() {
  VLOGF_ENTER();
  camera_mojo_channel_manager_ = CameraMojoChannelManager::CreateInstance();
  ipc_task_runner_ = camera_mojo_channel_manager_->GetIpcTaskRunner();

  LoadCameraHal();

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  if (!watcher_.Watch(socket_path, false,
                      base::Bind(&CameraHalServerImpl::OnSocketFileStatusChange,
                                 base::Unretained(this)))) {
    LOGF(ERROR) << "Failed to watch socket path";
    return false;
  }
  if (base::PathExists(socket_path)) {
    CameraHalServerImpl::OnSocketFileStatusChange(socket_path, false);
  }
  return true;
}

void CameraHalServerImpl::CreateChannel(
    mojom::CameraModuleRequest camera_module_request) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  camera_hal_adapter_->OpenCameraHal(std::move(camera_module_request));
}

void CameraHalServerImpl::SetTracingEnabled(bool enabled) {
  VLOGF_ENTER();
  TRACE_CAMERA_ENABLE(enabled);
}

void CameraHalServerImpl::OnSocketFileStatusChange(
    const base::FilePath& socket_path, bool error) {
  VLOGF_ENTER();
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  camera_mojo_channel_manager_->ConnectToDispatcher(
      base::Bind(&CameraHalServerImpl::RegisterCameraHal,
                 base::Unretained(this)),
      base::Bind(&CameraHalServerImpl::OnServiceMojoChannelError,
                 base::Unretained(this)));
}

void CameraHalServerImpl::LoadCameraHal() {
  VLOGF_ENTER();
  // We can't load and initialize the camera HALs on |ipc_task_runner_| since it
  // will cause dead-lock if any of the camera HAL initiates any Mojo connection
  // during initialization.
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  std::vector<camera_module_t*> camera_modules;
  std::unique_ptr<CameraConfig> config =
      CameraConfig::Create(constants::kCrosCameraTestConfigPathString);
  bool enable_front =
           config->GetBoolean(constants::kCrosEnableFrontCameraOption, true),
       enable_back =
           config->GetBoolean(constants::kCrosEnableBackCameraOption, true),
       enable_external =
           config->GetBoolean(constants::kCrosEnableExternalCameraOption, true);

  for (const auto& dll : GetCameraHalPaths()) {
    LOGF(INFO) << "Try to load camera hal " << dll.value();

    void* handle = dlopen(dll.value().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      LOGF(INFO) << "Failed to dlopen: " << dlerror();
      ExitOnMainThread(ENOENT);
    }

    auto* module = static_cast<camera_module_t*>(
        dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
    if (!module) {
      LOGF(ERROR) << "Failed to get camera_module_t pointer with symbol name "
                  << HAL_MODULE_INFO_SYM_AS_STR << " from " << dll.value();
      ExitOnMainThread(ELIBBAD);
    }

    camera_modules.push_back(module);
  }

  if (enable_front && enable_back && enable_external) {
    camera_hal_adapter_.reset(new CameraHalAdapter(camera_modules));
  } else {
    camera_hal_adapter_.reset(new CameraHalTestAdapter(
        camera_modules, enable_front, enable_back, enable_external));
  }

  LOGF(INFO) << "Running camera HAL adapter on " << getpid();

  if (!camera_hal_adapter_->Start()) {
    LOGF(ERROR) << "Failed to start camera HAL adapter";
    ExitOnMainThread(ENODEV);
  }
}

void CameraHalServerImpl::RegisterCameraHal() {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (binding_.is_bound()) {
    return;
  }

  mojom::CameraHalServerPtr server_ptr;
  binding_.Bind(mojo::MakeRequest(&server_ptr));
  camera_mojo_channel_manager_->RegisterServer(std::move(server_ptr));
  LOGF(INFO) << "Registered camera HAL";
}

void CameraHalServerImpl::OnServiceMojoChannelError() {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (!binding_.is_bound()) {
    // We reach here because |camera_mojo_channel_manager_| failed to bootstrap
    // the Mojo channel to Chrome, probably due to invalid socket file.  This
    // can happen during `restart ui`, so simply return to wait for Chrome to
    // reinitialize the socket file.
    return;
  }

  // The CameraHalDispatcher Mojo parent is probably dead. We need to restart
  // another process in order to connect to the new Mojo parent.
  LOGF(INFO) << "Mojo connection to CameraHalDispatcher is broken";
  main_task_runner_->PostTask(FROM_HERE,
                              base::Bind(&CameraHalServerImpl::ExitOnMainThread,
                                         base::Unretained(this), ECONNRESET));
}

void CameraHalServerImpl::ExitOnMainThread(int exit_status) {
  VLOGF_ENTER();
  DCHECK(main_task_runner_->BelongsToCurrentThread());

  camera_hal_adapter_.reset();
  exit(exit_status);
}

}  // namespace cros
