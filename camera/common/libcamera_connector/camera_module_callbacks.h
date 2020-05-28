/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_MODULE_CALLBACKS_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_MODULE_CALLBACKS_H_

#include <base/callback.h>
#include <mojo/public/cpp/bindings/associated_binding.h>

#include "mojo/camera_common.mojom.h"

namespace cros {

class CameraModuleCallbacks final : public mojom::CameraModuleCallbacks {
 public:
  using DeviceStatusCallback = base::Callback<void(int32_t, bool)>;

  explicit CameraModuleCallbacks(DeviceStatusCallback device_status_callback);

  // Resets the current associated binding |camera_module_callbacks_|, creates a
  // new associated CameraModuleCallbacks pointer info, binds the request and
  // returns the associated pointer info.
  mojom::CameraModuleCallbacksAssociatedPtrInfo GetModuleCallbacks();

  void CameraDeviceStatusChange(int32_t camera_id,
                                mojom::CameraDeviceStatus new_status) override;

  void TorchModeStatusChange(int32_t camera_id,
                             mojom::TorchModeStatus new_status) override;

 private:
  mojo::AssociatedBinding<mojom::CameraModuleCallbacks>
      camera_module_callbacks_;
  DeviceStatusCallback device_status_callback_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_MODULE_CALLBACKS_H_
