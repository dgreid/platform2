/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_MODULE_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_CAMERA_MODULE_DELEGATE_H_

#include "common/utils/cros_camera_mojo_utils.h"
#include "mojo/camera3.mojom.h"
#include "mojo/camera_common.mojom.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

class CameraHalAdapter;

class CameraModuleDelegate final
    : public internal::MojoBinding<mojom::CameraModule> {
 public:
  CameraModuleDelegate(CameraHalAdapter* camera_hal_adapter,
                       scoped_refptr<base::SingleThreadTaskRunner> task_runner,
                       mojom::CameraClientType camera_client_type);

  ~CameraModuleDelegate();

 private:
  void OpenDevice(int32_t camera_id,
                  mojom::Camera3DeviceOpsRequest device_ops_request,
                  OpenDeviceCallback callback) final;

  void GetNumberOfCameras(GetNumberOfCamerasCallback callback) final;

  void GetCameraInfo(int32_t camera_id, GetCameraInfoCallback callback) final;

  void SetCallbacks(mojom::CameraModuleCallbacksPtr callbacks,
                    SetCallbacksCallback callback) final;

  void SetTorchMode(int32_t camera_id,
                    bool enabled,
                    SetTorchModeCallback callback) final;

  void Init(InitCallback callback) final;

  void GetVendorTagOps(mojom::VendorTagOpsRequest vendor_tag_ops_request,
                       GetVendorTagOpsCallback callback) final;

  void SetCallbacksAssociated(
      mojom::CameraModuleCallbacksAssociatedPtrInfo callbacks_info,
      SetCallbacksAssociatedCallback callback) final;

  CameraHalAdapter* camera_hal_adapter_;
  mojom::CameraClientType camera_client_type_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CameraModuleDelegate);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_MODULE_DELEGATE_H_
