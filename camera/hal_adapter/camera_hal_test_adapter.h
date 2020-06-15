/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_HAL_TEST_ADAPTER_H_
#define CAMERA_HAL_ADAPTER_CAMERA_HAL_TEST_ADAPTER_H_

#include <utility>
#include <vector>

#include <hardware/camera3.h>

#include <base/optional.h>

#include "hal_adapter/camera_hal_adapter.h"

namespace cros {

class CameraHalTestAdapter : public CameraHalAdapter {
 public:
  CameraHalTestAdapter(std::vector<camera_module_t*> camera_modules,
                       bool enable_front,
                       bool enable_back,
                       bool enable_external);

  ~CameraHalTestAdapter() override {}

  int32_t OpenDevice(
      int32_t camera_id,
      mojom::Camera3DeviceOpsRequest device_ops_request) override;

  int32_t GetNumberOfCameras() override;

  int32_t GetCameraInfo(int32_t camera_id,
                        mojom::CameraInfoPtr* camera_info) override;

  int32_t SetTorchMode(int32_t camera_id, bool enabled) override;

 protected:
  void StartOnThread(base::Callback<void(bool)> callback) override;

  void NotifyCameraDeviceStatusChange(CameraModuleCallbacksDelegate* delegate,
                                      int camera_id,
                                      camera_device_status_t status) override;

  void NotifyTorchModeStatusChange(CameraModuleCallbacksDelegate* delegate,
                                   int camera_id,
                                   torch_mode_status_t status) override;

 private:
  bool enable_front_, enable_back_, enable_external_;

  // Id of enabled cameras assigned by SuperHAL. |CameraHalTestAdapter| will
  // reassign new id exposed to framework based on its index in this vector.
  std::vector<int> enable_camera_ids_;

  base::Optional<int32_t> GetRemappedCameraId(int camera_id);

  base::Optional<int32_t> GetUnRemappedCameraId(int camera_id);

  DISALLOW_IMPLICIT_CONSTRUCTORS(CameraHalTestAdapter);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_HAL_TEST_ADAPTER_H_
