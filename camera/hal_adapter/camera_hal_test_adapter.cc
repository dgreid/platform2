/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_hal_test_adapter.h"

#include <string>

#include "cros-camera/future.h"

namespace cros {

CameraHalTestAdapter::CameraHalTestAdapter(
    std::vector<camera_module_t*> camera_modules,
    bool enable_front,
    bool enable_back,
    bool enable_external)
    : CameraHalAdapter(camera_modules),
      enable_front_(enable_front),
      enable_back_(enable_back),
      enable_external_(enable_external) {
  VLOGF_ENTER();
  LOGF(INFO) << "Filter options: enable_front=" << enable_front_
             << ", enable_back=" << enable_back_
             << ", enable_external=" << enable_external_;
}

int32_t CameraHalTestAdapter::OpenDevice(
    int32_t camera_id, mojom::Camera3DeviceOpsRequest device_ops_request) {
  VLOGF_ENTER();

  base::Optional<int> unremapped_id = GetUnRemappedCameraId(camera_id);
  if (!unremapped_id) {
    return -EINVAL;
  }
  LOGF(INFO) << "From remap camera id " << camera_id << " to "
             << *unremapped_id;
  return CameraHalAdapter::OpenDevice(*unremapped_id,
                                      std::move(device_ops_request));
}

int32_t CameraHalTestAdapter::GetNumberOfCameras() {
  VLOGF_ENTER();

  return enable_camera_ids_.size();
}

int32_t CameraHalTestAdapter::GetCameraInfo(int32_t camera_id,
                                            mojom::CameraInfoPtr* camera_info) {
  VLOGF_ENTER();

  base::Optional<int> unremapped_id = GetUnRemappedCameraId(camera_id);
  if (!unremapped_id) {
    camera_info->reset();
    return -EINVAL;
  }
  LOGF(INFO) << "From remap camera id " << camera_id << " to "
             << *unremapped_id;
  int32_t ret = CameraHalAdapter::GetCameraInfo(*unremapped_id, camera_info);
  if (ret != 0) {
    return ret;
  }
  if ((*camera_info)->conflicting_devices) {
    std::vector<std::string> conflicting_devices;
    for (const std::string& id_str : *(*camera_info)->conflicting_devices) {
      base::Optional<int> remapped_id = GetRemappedCameraId(std::stoi(id_str));
      if (remapped_id) {
        conflicting_devices.push_back(std::to_string(*remapped_id));
      }
    }
    (*camera_info)->conflicting_devices = std::move(conflicting_devices);
  }
  return 0;
}

int32_t CameraHalTestAdapter::SetTorchMode(int32_t camera_id, bool enabled) {
  VLOGF_ENTER();

  base::Optional<int> unremapped_id = GetUnRemappedCameraId(camera_id);
  if (!unremapped_id) {
    return -EINVAL;
  }
  LOGF(INFO) << "From remap camera id " << camera_id << " to "
             << *unremapped_id;
  return CameraHalAdapter::SetTorchMode(*unremapped_id, enabled);
}

void CameraHalTestAdapter::StartOnThread(base::Callback<void(bool)> callback) {
  VLOGF_ENTER();

  auto future = cros::Future<bool>::Create(nullptr);
  CameraHalAdapter::StartOnThread(cros::GetFutureCallback(future));

  if (!future.get()) {
    callback.Run(false);
    return;
  }

  for (int cam_id = 0; cam_id < CameraHalAdapter::GetNumberOfCameras();
       cam_id++) {
    camera_module_t* m;
    int internal_id;
    std::tie(m, internal_id) = CameraHalAdapter::GetInternalModuleAndId(cam_id);

    camera_info_t info;
    int ret = m->get_camera_info(internal_id, &info);
    if (ret != 0) {
      LOGF(ERROR) << "Failed to get info of camera " << cam_id;
      callback.Run(false);
      return;
    }

    if ((info.facing == CAMERA_FACING_BACK && enable_back_) ||
        (info.facing == CAMERA_FACING_FRONT && enable_front_)) {
      LOGF(INFO) << "Remap camera id " << cam_id << "->"
                 << enable_camera_ids_.size();
      enable_camera_ids_.push_back(cam_id);
    } else {
      LOG(INFO) << "Filter out camera " << internal_id << " facing "
                << info.facing << " of module " << m->common.name;
    }
  }
  LOGF(INFO) << "Enable total " << enable_camera_ids_.size() << " cameras";
  callback.Run(true);
}

void CameraHalTestAdapter::NotifyCameraDeviceStatusChange(
    CameraModuleCallbacksDelegate* delegate,
    int camera_id,
    camera_device_status_t status) {
  VLOGF_ENTER();

  base::Optional<int> remapped_id = GetRemappedCameraId(camera_id);
  if (remapped_id) {
    LOGF(INFO) << "Remap external camera id " << camera_id << "->"
               << *remapped_id;
    CameraHalAdapter::NotifyCameraDeviceStatusChange(delegate, *remapped_id,
                                                     status);
  }
}

void CameraHalTestAdapter::NotifyTorchModeStatusChange(
    CameraModuleCallbacksDelegate* delegate,
    int camera_id,
    torch_mode_status_t status) {
  VLOGF_ENTER();

  base::Optional<int> remapped_id = GetRemappedCameraId(camera_id);
  if (remapped_id) {
    CameraHalAdapter::NotifyTorchModeStatusChange(delegate, *remapped_id,
                                                  status);
  }
}

base::Optional<int> CameraHalTestAdapter::GetUnRemappedCameraId(int camera_id) {
  if (camera_id < 0) {
    LOGF(ERROR) << "Invalid remapped camera id: " << camera_id;
    return {};
  }
  if (camera_id < enable_camera_ids_.size()) {
    return enable_camera_ids_[camera_id];
  } else if (enable_external_) {
    return camera_id - GetNumberOfCameras() +
           CameraHalAdapter::GetNumberOfCameras();
  } else {
    return {};
  }
}

base::Optional<int> CameraHalTestAdapter::GetRemappedCameraId(int camera_id) {
  if (camera_id < 0) {
    LOGF(ERROR) << "Invalid unremapped camera id: " << camera_id;
    return {};
  }
  if (camera_id < CameraHalAdapter::GetNumberOfCameras()) {
    auto it = std::find(enable_camera_ids_.begin(), enable_camera_ids_.end(),
                        camera_id);
    return it != enable_camera_ids_.end() ? it - enable_camera_ids_.begin()
                                          : base::Optional<int>{};
  } else if (enable_external_) {
    return camera_id - CameraHalAdapter::GetNumberOfCameras() +
           GetNumberOfCameras();
  } else {
    return {};
  }
}

}  // namespace cros
