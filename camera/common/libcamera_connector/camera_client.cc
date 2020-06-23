/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/camera_client.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/posix/safe_strerror.h>

#include "common/libcamera_connector/camera_metadata_utils.h"
#include "common/libcamera_connector/supported_formats.h"
#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"

namespace {

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

int GetCameraFacing(const cros::mojom::CameraInfoPtr& info) {
  switch (info->facing) {
    case cros::mojom::CameraFacing::CAMERA_FACING_BACK:
      return CROS_CAM_FACING_BACK;
    case cros::mojom::CameraFacing::CAMERA_FACING_FRONT:
      return CROS_CAM_FACING_FRONT;
    case cros::mojom::CameraFacing::CAMERA_FACING_EXTERNAL:
      return CROS_CAM_FACING_EXTERNAL;
    default:
      LOGF(ERROR) << "unknown facing " << info->facing;
      return CROS_CAM_FACING_EXTERNAL;
  }
}

}  // namespace

namespace cros {

CameraClient::CameraClient()
    : ipc_thread_("CamClient"),
      camera_hal_client_(this),
      cam_info_callback_(nullptr),
      capture_started_(false) {}

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

int CameraClient::Exit() {
  VLOGF_ENTER();
  int ret = 0;
  {
    base::AutoLock l(capture_started_lock_);
    if (capture_started_) {
      auto future = cros::Future<int>::Create(nullptr);
      stop_callback_ = cros::GetFutureCallback(future);
      client_ops_.StopCapture(base::Bind(&CameraClient::OnClosedDevice,
                                         base::Unretained(this),
                                         /*is_local_stop=*/false));
      ret = future->Get();
    }
  }

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::CloseOnThread, base::Unretained(this)));
  ipc_thread_.Stop();

  return ret;
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

int CameraClient::StartCapture(const cros_cam_capture_request_t* request,
                               cros_cam_capture_cb_t callback,
                               void* context) {
  VLOGF_ENTER();

  base::AutoLock l(capture_started_lock_);
  if (capture_started_) {
    LOGF(WARNING) << "Capture already started";
    return -EINVAL;
  }
  if (!IsDeviceActive(request->id)) {
    LOGF(ERROR) << "Cannot start capture on an inactive device: "
                << request->id;
    return -ENODEV;
  }

  LOGF(INFO) << "Starting capture";

  // TODO(b/151047930): Check whether this format info is actually supported.
  request_camera_id_ = request->id;
  request_format_ = *request->format;
  request_callback_ = callback;
  request_context_ = context;

  auto future = cros::Future<int>::Create(nullptr);
  start_callback_ = cros::GetFutureCallback(future);

  client_ops_.Init(
      base::BindOnce(&CameraClient::OnDeviceOpsReceived,
                     base::Unretained(this)),
      base::Bind(&CameraClient::SendCaptureResult, base::Unretained(this)));

  return future->Get();
}

int CameraClient::StopCapture(int id) {
  VLOGF_ENTER();

  base::AutoLock l(capture_started_lock_);
  if (!capture_started_) {
    LOGF(WARNING) << "Capture already stopped";
    return -EPERM;
  }
  if (!IsDeviceActive(id)) {
    LOGF(ERROR) << "Cannot stop capture on an inactive device: " << id;
    return -ENODEV;
  }

  // TODO(lnishan): Support multi-device streaming.
  CHECK_EQ(request_camera_id_, id);

  LOGF(INFO) << "Stopping capture";

  auto future = cros::Future<int>::Create(nullptr);
  stop_callback_ = cros::GetFutureCallback(future);
  client_ops_.StopCapture(base::Bind(&CameraClient::OnClosedDevice,
                                     base::Unretained(this),
                                     /*is_local_stop=*/false));
  return future->Get();
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
    LOGF(ERROR) << "Failed to get camera info of " << camera_id << ": "
                << base::safe_strerror(-result);
    std::move(init_callback_).Run(-ENODEV);
    return;
  }

  LOGF(INFO) << "Gotten camera info of " << camera_id;

  auto& camera_info = camera_info_map_[camera_id];
  camera_info.facing = GetCameraFacing(info);
  camera_info.name = GetCameraName(info);

  auto& format_info = camera_info_map_[camera_id].format_info;
  auto min_frame_durations = GetMetadataEntryAsSpan<int64_t>(
      info->static_camera_characteristics,
      mojom::CameraMetadataTag::ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  for (size_t i = 0; i < min_frame_durations.size(); i += 4) {
    int64_t hal_pixel_format = min_frame_durations[i + 0];
    int64_t width = min_frame_durations[i + 1];
    int64_t height = min_frame_durations[i + 2];
    int64_t duration_ns = min_frame_durations[i + 3];

    uint32_t fourcc = GetV4L2PixelFormat(hal_pixel_format);
    if (fourcc == 0) {
      VLOGF(1) << "Skip unsupported format " << hal_pixel_format;
      continue;
    }

    cros_cam_format_info_t info = {
        .fourcc = fourcc,
        .width = static_cast<int>(width),
        .height = static_cast<int>(height),
        .fps = static_cast<int>(round(1e9 / duration_ns)),
    };
    format_info.push_back(std::move(info));
  }

  camera_info.jpeg_max_size = GetMetadataEntryAsSpan<int32_t>(
      info->static_camera_characteristics,
      mojom::CameraMetadataTag::ANDROID_JPEG_MAX_SIZE)[0];

  ++camera_id_iter_;
  if (camera_id_iter_ == camera_id_list_.end()) {
    std::move(init_callback_).Run(0);
  } else {
    GetCameraInfo(*camera_id_iter_);
  }
}

void CameraClient::SendCameraInfo() {
  VLOGF_ENTER();

  if (cam_info_callback_ == nullptr) {
    return;
  }

  for (auto& camera_id : camera_id_list_) {
    auto it = camera_info_map_.find(camera_id);
    if (camera_info_map_.find(camera_id) == camera_info_map_.end()) {
      LOGF(ERROR) << "Cannot find the info of camera " << camera_id;
      continue;
    }
    cros_cam_info_t cam_info = {
        .id = camera_id,
        .facing = it->second.facing,
        .name = it->second.name.c_str(),
        .format_count = static_cast<int>(it->second.format_info.size()),
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

void CameraClient::OnDeviceOpsReceived(
    mojom::Camera3DeviceOpsRequest device_ops_request) {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::OpenDeviceOnThread, base::Unretained(this),
                     std::move(device_ops_request)));
}

void CameraClient::OpenDeviceOnThread(
    mojom::Camera3DeviceOpsRequest device_ops_request) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->OpenDevice(
      request_camera_id_, std::move(device_ops_request),
      base::Bind(&CameraClient::OnOpenedDevice, base::Unretained(this)));
}

void CameraClient::OnOpenedDevice(int32_t result) {
  if (result != 0) {
    LOGF(ERROR) << "Failed to open camera " << request_camera_id_;
  } else {
    LOGF(INFO) << "Camera opened successfully";
    client_ops_.StartCapture(
        request_camera_id_, &request_format_,
        camera_info_map_[request_camera_id_].jpeg_max_size);
    // Caller should hold |capture_started_lock_| until the device is opened.
    CHECK(!capture_started_lock_.Try());
    capture_started_ = true;
  }
  std::move(start_callback_).Run(result);
}

void CameraClient::OnClosedDevice(bool is_local_stop, int32_t result) {
  if (result != 0) {
    LOGF(ERROR) << "Failed to close camera " << request_camera_id_;
  } else {
    LOGF(INFO) << "Camera closed successfully";
  }
  // Caller should hold |capture_started_lock_| until the device is closed.
  CHECK(!capture_started_lock_.Try());
  // Capture is marked stopped regardless of the result. When an error takes
  // place, we don't want to close or use the camera again.
  capture_started_ = false;
  if (is_local_stop) {
    // If the stop was initiated through CameraClientOps, the root
    // StopCapture() would be called on |ops_thread_| holding
    // |capture_started_lock_|. We release it here to allow further
    // StartCapture() and StopCapture() calls to resume.
    capture_started_lock_.Release();
  } else {
    // If the stop was initiated by a client (through StopCapture()) or Exit()
    // call, it would come from a different thread, and thus we cannot release
    // |capture_started_lock_| here. The caller would set a future callback,
    // |stop_callback_| and wait on it.
    std::move(stop_callback_).Run(result);
  }
}

bool CameraClient::IsDeviceActive(int device) {
  return camera_info_map_.find(device) != camera_info_map_.end();
}

void CameraClient::SendCaptureResult(const cros_cam_capture_result_t& result) {
  // Make sure cameras aren't being opened or stopped. It's very important we
  // don't wait on the lock here. If we waited on the lock, the thread owned by
  // CameraClientOps would be blocked. If StopCapture() was the one which
  // acquired the lock, it would hold it until device is closed. Since
  // Camera3DeviceOps::Close() is done on CameraClientOps thread, it would not
  // be able to continue if we were to wait on the lock here, causing deadlock.
  if (!capture_started_lock_.Try()) {
    VLOGF(1) << "Capture is being started or stopped. Dropping a frame.";
    return;
  }
  if (!capture_started_) {
    LOGF(INFO) << "Camera already closed. Skipping a capture result.";
    capture_started_lock_.Release();
    return;
  }
  int ret = (*request_callback_)(request_context_, &result);
  if (ret != 0) {
    client_ops_.StopCapture(base::Bind(&CameraClient::OnClosedDevice,
                                       base::Unretained(this),
                                       /*is_local_stop=*/true));
    return;
  }
  capture_started_lock_.Release();
}

}  // namespace cros
