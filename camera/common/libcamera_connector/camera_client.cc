/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/camera_client.h"

#include <cmath>
#include <utility>

#include <base/bind.h>
#include <base/containers/span.h>
#include <hardware/gralloc.h>

#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace {

template <typename T>
base::span<T> GetMetadataEntryAsSpan(
    const cros::mojom::CameraMetadataPtr& camera_metadata,
    cros::mojom::CameraMetadataTag tag) {
  CHECK(!camera_metadata.is_null());
  auto& entries = camera_metadata->entries;
  CHECK(entries.has_value());
  for (auto& entry : *entries) {
    if (entry->tag == tag) {
      auto& data = entry->data;
      CHECK_EQ(data.size() % sizeof(T), 0u);
      return {reinterpret_cast<T*>(data.data()), data.size() / sizeof(T)};
    }
  }
  return {};
}

std::string GetCameraName(const cros::mojom::CameraInfoPtr& info) {
  switch (info->facing) {
    case cros::mojom::CameraFacing::CAMERA_FACING_BACK:
      return "Back Camera";
    case cros::mojom::CameraFacing::CAMERA_FACING_FRONT:
      return "Front Camera";
    case cros::mojom::CameraFacing::CAMERA_FACING_EXTERNAL:
      return "External Camera";
    default:
      return "Unknown Camera";
  }
}

}  // namespace

namespace cros {

CameraClient::CameraClient()
    : ipc_thread_("CamClient"),
      camera_hal_client_(this),
      cam_info_callback_(nullptr) {}

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
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  LOGF(INFO) << "Received camera module from camera HAL dispatcher";
  camera_module_ = std::move(camera_module);

  GetNumberOfCameras();
}

int CameraClient::SetCameraInfoCallback(cros_cam_get_cam_info_cb_t callback,
                                        void* context) {
  VLOGF_ENTER();

  cam_info_callback_ = callback;
  cam_info_context_ = context;

  SendCameraInfo();
  return 0;
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

void CameraClient::GetNumberOfCameras() {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->GetNumberOfCameras(
      base::Bind(&CameraClient::OnGotNumberOfCameras, base::Unretained(this)));
}

void CameraClient::OnGotNumberOfCameras(int32_t num_builtin_cameras) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  num_builtin_cameras_ = num_builtin_cameras;
  LOGF(INFO) << "Number of builtin cameras: " << num_builtin_cameras_;

  for (int32_t i = 0; i < num_builtin_cameras_; ++i) {
    camera_id_list_.push_back(i);
  }
  if (num_builtin_cameras_ == 0) {
    std::move(init_callback_).Run(0);
    return;
  }
  camera_id_iter_ = camera_id_list_.begin();
  GetCameraInfo(*camera_id_iter_);
}

void CameraClient::GetCameraInfo(int32_t camera_id) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->GetCameraInfo(
      camera_id,
      base::Bind(&CameraClient::OnGotCameraInfo, base::Unretained(this)));
}

void CameraClient::OnGotCameraInfo(int32_t result, mojom::CameraInfoPtr info) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  int32_t camera_id = *camera_id_iter_;
  if (result != 0) {
    LOGF(ERROR) << "Failed to get camera info of " << camera_id;
    std::move(init_callback_).Run(-ENODEV);
    return;
  }

  LOGF(INFO) << "Gotten camera info of " << camera_id;

  auto& camera_info = camera_info_map_[camera_id];
  camera_info.name = GetCameraName(info);

  auto& format_info = camera_info_map_[camera_id].format_info;
  auto* buffer_manager = cros::CameraBufferManager::GetInstance();
  auto min_frame_durations = GetMetadataEntryAsSpan<int64_t>(
      info->static_camera_characteristics,
      mojom::CameraMetadataTag::ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  for (size_t i = 0; i < min_frame_durations.size(); i += 4) {
    uint32_t drm_format = buffer_manager->ResolveDrmFormat(
        min_frame_durations[i + 0],
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
    if (drm_format == 0) {  // Failed to resolve to a format
      continue;
    }
    cros_cam_format_info_t info = {
        .fourcc = drm_format,
        .width = static_cast<unsigned>(min_frame_durations[i + 1]),
        .height = static_cast<unsigned>(min_frame_durations[i + 2]),
        .fps = static_cast<unsigned>(round(1e9 / min_frame_durations[i + 3]))};
    format_info.push_back(std::move(info));
  }

  ++camera_id_iter_;
  if (camera_id_iter_ == camera_id_list_.end()) {
    std::move(init_callback_).Run(0);
  } else {
    GetCameraInfo(*camera_id_iter_);
  }
}

void CameraClient::SendCameraInfo() {
  VLOGF_ENTER();

  for (auto& camera_id : camera_id_list_) {
    auto it = camera_info_map_.find(camera_id);
    if (camera_info_map_.find(camera_id) == camera_info_map_.end()) {
      LOGF(ERROR) << "Cannot find the info of camera " << camera_id;
      continue;
    }
    cros_cam_info_t cam_info = {
        .id = reinterpret_cast<void*>(&camera_id),
        .name = it->second.name.c_str(),
        .format_count = static_cast<unsigned>(it->second.format_info.size()),
        .format_info = it->second.format_info.data()};

    int ret =
        (*cam_info_callback_)(cam_info_context_, &cam_info, /*is_removed=*/0);
    if (ret != 0) {
      // Deregister callback
      cam_info_callback_ = nullptr;
      cam_info_context_ = nullptr;
      break;
    }
  }
}

}  // namespace cros
