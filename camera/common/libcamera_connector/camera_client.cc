/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/camera_client.h"

#include <utility>

#include <base/bind.h>
#include <base/synchronization/waitable_event.h>

#include "common/libcamera_connector/types.h"
#include "cros-camera/common.h"

namespace cros {

CameraClient::CameraClient()
    : ipc_thread_("CamClient"), camera_hal_client_(this) {}

void CameraClient::Init(RegisterClientCallback register_client_callback,
                        IntOnceCallback init_callback) {
  bool ret = ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
  if (!ret) {
    LOGF(ERROR) << "Failed to start IPC thread";
    std::move(init_callback).Run(-ENODEV);
    return;
  }
  init_callback_ = std::move(init_callback);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::RegisterClient, base::Unretained(this),
                     std::move(register_client_callback)));
}

void CameraClient::Exit() {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::CloseOnThread, base::Unretained(this)));
  ipc_thread_.Stop();
}

void CameraClient::SetUpChannel(mojom::CameraModulePtr camera_module) {
  VLOGF_ENTER();
  LOGF(INFO) << "Got CameraModulePtr";
  std::move(init_callback_).Run(0);
}

void CameraClient::RegisterClient(
    RegisterClientCallback register_client_callback) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  mojom::CameraHalClientPtr client_ptr;
  camera_hal_client_.Bind(mojo::MakeRequest(&client_ptr));
  std::move(register_client_callback).Run(std::move(client_ptr));
}

void CameraClient::CloseOnThread() {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_hal_client_.Close();
}

}  // namespace cros
