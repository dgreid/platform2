/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_

#include <memory>

#include <base/bind.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/types.h"
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

 private:
  // Registers the client at camera HAL dispatcher.
  void RegisterClient(RegisterClientCallback register_client_callback);

  // Closes the message pipe associated with this client.
  void CloseOnThread();

  base::Thread ipc_thread_;

  mojo::Binding<mojom::CameraHalClient> camera_hal_client_;

  IntOnceCallback init_callback_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
