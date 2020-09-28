/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_module_callbacks_associated_delegate.h"

#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>

#include "cros-camera/common.h"
#include "cros-camera/future.h"

namespace cros {

CameraModuleCallbacksAssociatedDelegate::
    CameraModuleCallbacksAssociatedDelegate(
        scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : internal::MojoAssociatedChannel<mojom::CameraModuleCallbacks>(
          task_runner) {}

void CameraModuleCallbacksAssociatedDelegate::CameraDeviceStatusChange(
    int camera_id, int new_status) {
  VLOGF_ENTER();
  auto future = cros::Future<void>::Create(&relay_);
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&CameraModuleCallbacksAssociatedDelegate::
                                CameraDeviceStatusChangeOnThread,
                            base::AsWeakPtr(this), camera_id, new_status,
                            cros::GetFutureCallback(future)));
  future->Wait();
}

void CameraModuleCallbacksAssociatedDelegate::TorchModeStatusChange(
    int camera_id, int new_status) {
  VLOGF_ENTER();
  auto future = cros::Future<void>::Create(&relay_);
  task_runner_->PostTask(
      FROM_HERE, base::Bind(&CameraModuleCallbacksAssociatedDelegate::
                                TorchModeStatusChangeOnThread,
                            base::AsWeakPtr(this), camera_id, new_status,
                            cros::GetFutureCallback(future)));
  future->Wait();
}

void CameraModuleCallbacksAssociatedDelegate::CameraDeviceStatusChangeOnThread(
    int camera_id, int new_status, base::Closure callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  interface_ptr_->CameraDeviceStatusChange(
      camera_id, static_cast<mojom::CameraDeviceStatus>(new_status));
  callback.Run();
}

void CameraModuleCallbacksAssociatedDelegate::TorchModeStatusChangeOnThread(
    int camera_id, int new_status, base::Closure callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  interface_ptr_->TorchModeStatusChange(
      camera_id, static_cast<mojom::TorchModeStatus>(new_status));
  callback.Run();
}

}  // namespace cros
