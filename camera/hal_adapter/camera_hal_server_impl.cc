/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_hal_server_impl.h"

#include <dlfcn.h>
#include <fcntl.h>
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
#include <base/posix/safe_strerror.h>
#include <base/threading/thread_task_runner_handle.h>

#include "common/camera_mojo_channel_manager_impl.h"
#include "common/utils/camera_hal_enumerator.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"
#include "cros-camera/utils/camera_config.h"
#include "hal_adapter/camera_hal_test_adapter.h"
#include "hal_adapter/camera_trace_event.h"

namespace cros {

CameraHalServerImpl::CameraHalServerImpl()
    : mojo_manager_(CameraMojoChannelManager::FromToken(
          CameraMojoChannelManagerToken::CreateInstance())),
      ipc_bridge_(new IPCBridge(this, mojo_manager_.get())) {
  VLOGF_ENTER();
}

CameraHalServerImpl::~CameraHalServerImpl() {
  VLOGF_ENTER();

  ExitOnMainThread(0);
}

void CameraHalServerImpl::Start() {
  VLOGF_ENTER();
  base::AutoLock l(ipc_bridge_lock_);

  int result = LoadCameraHal();
  if (result != 0) {
    ExitOnMainThread(result);
  }

  // We assume that |camera_hal_adapter_| will only be set once. If the
  // assumption changed, we should consider another way to provide
  // CameraHalAdapter.
  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraHalServerImpl::IPCBridge::Start,
                 ipc_bridge_->GetWeakPtr(), camera_hal_adapter_.get(),
                 base::BindRepeating(
                     [](const std::vector<cros_camera_hal_t*>& hals,
                        PrivacySwitchStateChangeCallback callback) {
                       for (const auto* hal : hals) {
                         if (hal->set_privacy_switch_callback != nullptr) {
                           hal->set_privacy_switch_callback(
                               std::move(callback));
                         }
                       }
                     },
                     cros_camera_hals_)));
}

CameraHalServerImpl::IPCBridge::IPCBridge(
    CameraHalServerImpl* camera_hal_server,
    CameraMojoChannelManager* mojo_manager)
    : camera_hal_server_(camera_hal_server),
      mojo_manager_(mojo_manager),
      ipc_task_runner_(mojo_manager_->GetIpcTaskRunner()),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      binding_(this) {}

CameraHalServerImpl::IPCBridge::~IPCBridge() {
  if (binding_.is_bound()) {
    binding_.Unbind();
  }
  callbacks_.reset();
}

void CameraHalServerImpl::IPCBridge::Start(
    CameraHalAdapter* camera_hal_adapter,
    SetPrivacySwitchCallback set_privacy_switch_callback) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (binding_.is_bound()) {
    return;
  }

  camera_hal_adapter_ = camera_hal_adapter;

  mojom::CameraHalServerPtr server_ptr;
  binding_.Bind(mojo::MakeRequest(&server_ptr));
  binding_.set_connection_error_handler(
      base::Bind(&CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError,
                 GetWeakPtr()));
  mojo_manager_->RegisterServer(
      std::move(server_ptr),
      base::BindOnce(&CameraHalServerImpl::IPCBridge::OnServerRegistered,
                     GetWeakPtr(), std::move(set_privacy_switch_callback)),
      base::BindOnce(&CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError,
                     GetWeakPtr()));
}

void CameraHalServerImpl::IPCBridge::CreateChannel(
    mojom::CameraModuleRequest camera_module_request,
    mojom::CameraClientType camera_client_type) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  camera_hal_adapter_->OpenCameraHal(std::move(camera_module_request),
                                     camera_client_type);
}

void CameraHalServerImpl::IPCBridge::SetTracingEnabled(bool enabled) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  TRACE_CAMERA_ENABLE(enabled);
}

void CameraHalServerImpl::IPCBridge::NotifyCameraActivityChange(
    int32_t camera_id, bool opened, mojom::CameraClientType type) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(callbacks_.is_bound());

  callbacks_->CameraDeviceActivityChange(camera_id, opened, type);
}

base::WeakPtr<CameraHalServerImpl::IPCBridge>
CameraHalServerImpl::IPCBridge::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void CameraHalServerImpl::IPCBridge::OnServerRegistered(
    SetPrivacySwitchCallback set_privacy_switch_callback,
    int32_t result,
    mojom::CameraHalServerCallbacksPtr callbacks) {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (result != 0) {
    LOGF(ERROR) << "Failed to register camera HAL: "
                << base::safe_strerror(-result);
    return;
  }
  callbacks_.Bind(callbacks.PassInterface());

  std::move(set_privacy_switch_callback)
      .Run(base::BindRepeating(
          &CameraHalServerImpl::IPCBridge::OnPrivacySwitchStatusChanged,
          base::Unretained(this)));

  LOGF(INFO) << "Registered camera HAL";
}

void CameraHalServerImpl::IPCBridge::OnServiceMojoChannelError() {
  VLOGF_ENTER();
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  // The CameraHalDispatcher Mojo parent is probably dead. We need to restart
  // another process in order to connect to the new Mojo parent.
  LOGF(INFO) << "Mojo connection to CameraHalDispatcher is broken";
  main_task_runner_->PostTask(
      FROM_HERE, base::Bind(&CameraHalServerImpl::ExitOnMainThread,
                            base::Unretained(camera_hal_server_), ECONNRESET));
}

void CameraHalServerImpl::IPCBridge::OnPrivacySwitchStatusChanged(
    PrivacySwitchState state) {
  cros::mojom::CameraPrivacySwitchState state_in_mojo;
  if (state == PrivacySwitchState::kUnknown) {
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::UNKNOWN;
  } else if (state == PrivacySwitchState::kOn) {
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::ON;
  } else {  // state == PrivacySwitchState::kOff
    state_in_mojo = cros::mojom::CameraPrivacySwitchState::OFF;
  }
  callbacks_->CameraPrivacySwitchStateChange(state_in_mojo);
}

int CameraHalServerImpl::LoadCameraHal() {
  VLOGF_ENTER();
  DCHECK(!camera_hal_adapter_);
  DCHECK_EQ(cros_camera_hals_.size(), 0);

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
      return ENOENT;
    }

    cros_camera_hal_t* cros_camera_hal = static_cast<cros_camera_hal_t*>(
        dlsym(handle, CROS_CAMERA_HAL_INFO_SYM_AS_STR));
    if (!cros_camera_hal) {
      // TODO(b/151270948): We should report error here once all camera HALs
      // have implemented the interface.
    } else {
      cros_camera_hal->set_up(mojo_manager_.get());
      cros_camera_hals_.push_back(cros_camera_hal);
    }

    auto* module = static_cast<camera_module_t*>(
        dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
    if (!module) {
      LOGF(ERROR) << "Failed to get camera_module_t pointer with symbol name "
                  << HAL_MODULE_INFO_SYM_AS_STR << " from " << dll.value();
      return ELIBBAD;
    }

    camera_modules.push_back(module);
  }

  auto active_callback = base::Bind(
      &CameraHalServerImpl::OnCameraActivityChange, base::Unretained(this));
  if (enable_front && enable_back && enable_external) {
    camera_hal_adapter_.reset(new CameraHalAdapter(
        camera_modules, mojo_manager_.get(), active_callback));
  } else {
    camera_hal_adapter_.reset(new CameraHalTestAdapter(
        camera_modules, mojo_manager_.get(), active_callback, enable_front,
        enable_back, enable_external));
  }

  LOGF(INFO) << "Running camera HAL adapter on " << getpid();

  if (!camera_hal_adapter_->Start()) {
    LOGF(ERROR) << "Failed to start camera HAL adapter";
    return ENODEV;
  }

  return 0;
}

void CameraHalServerImpl::ExitOnMainThread(int exit_status) {
  VLOGF_ENTER();
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  base::AutoLock l(ipc_bridge_lock_);

  for (auto* cros_camera_hal : cros_camera_hals_) {
    cros_camera_hal->tear_down();
  }

  auto future = Future<void>::Create(nullptr);
  auto delete_ipc_bridge = base::BindOnce(
      [](std::unique_ptr<IPCBridge> ipc_bridge,
         base::Callback<void(void)> callback) { std::move(callback).Run(); },
      std::move(ipc_bridge_), cros::GetFutureCallback(future));
  mojo_manager_->GetIpcTaskRunner()->PostTask(FROM_HERE,
                                              std::move(delete_ipc_bridge));
  future->Wait(-1);

  // To make sure all the devices are properly closed before triggering the exit
  // handlers on Camera HALs side, we explicitly reset the CameraHalAdapter.
  camera_hal_adapter_.reset();

  exit(exit_status);
}

void CameraHalServerImpl::OnCameraActivityChange(int32_t camera_id,
                                                 bool opened,
                                                 mojom::CameraClientType type) {
  base::AutoLock l(ipc_bridge_lock_);
  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &CameraHalServerImpl::IPCBridge::NotifyCameraActivityChange,
          ipc_bridge_->GetWeakPtr(), camera_id, opened, type));
}

}  // namespace cros
