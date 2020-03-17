/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_

#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_service_connector.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

class CameraClient final : public mojom::CameraHalClient {
 public:
  using RegisterClientCallback =
      base::OnceCallback<void(mojom::CameraHalClientPtr)>;

  CameraClient();

  // Starts the thread and initializes the HAL client.
  void Init(RegisterClientCallback register_client_callback,
            IntOnceCallback init_callback);

  // Disconnects the client from camera HAL dispatcher.
  void Exit();

  // Implementation of cros::mojom::CameraHalClient. Called by camera HAL
  // dispatcher to provide the camera module interface.
  void SetUpChannel(mojom::CameraModulePtr camera_module) override;

  // Sets the callback for camera info changes and fires |callback| with the
  // info of the cameras currently present.
  int SetCameraInfoCallback(cros_cam_get_cam_info_cb_t callback, void* context);

 private:
  struct CameraInfo {
    std::string name;
    std::vector<cros_cam_format_info_t> format_info;
  };

  // Registers the client at camera HAL dispatcher.
  void RegisterClient(RegisterClientCallback register_client_callback);

  // Closes the message pipe associated with this client.
  void CloseOnThread();

  void GetNumberOfCameras();

  void OnGotNumberOfCameras(int32_t num_builtin_cameras);

  void GetCameraInfo(int32_t camera_id);

  void OnGotCameraInfo(int32_t result, mojom::CameraInfoPtr info);

  void SendCameraInfo();

  base::Thread ipc_thread_;

  mojom::CameraModulePtr camera_module_;
  mojo::Binding<mojom::CameraHalClient> camera_hal_client_;

  IntOnceCallback init_callback_;

  cros_cam_get_cam_info_cb_t cam_info_callback_;
  void* cam_info_context_;

  int32_t num_builtin_cameras_;

  std::list<int32_t> camera_id_list_;
  std::list<int32_t>::iterator camera_id_iter_;
  std::map<int32_t, CameraInfo> camera_info_map_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
