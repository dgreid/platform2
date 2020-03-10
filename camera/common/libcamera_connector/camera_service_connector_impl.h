/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_SERVICE_CONNECTOR_IMPL_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_SERVICE_CONNECTOR_IMPL_H_

#include <memory>

#include <base/bind.h>
#include <base/sequence_checker.h>
#include <base/synchronization/waitable_event.h>
#include <base/threading/thread.h>
#include <base/time/time.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/camera_client.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

class CameraServiceConnector {
 public:
  CameraServiceConnector();

  static CameraServiceConnector* GetInstance();

  // Initializes the connection to camera HAL dispatcher and registers the
  // camera HAL client. Must be called before any other functions.
  void Init(IntOnceCallback init_callback);

  // Terminates camera HAL client, all connections and threads.
  void Exit();

 private:
  using ConnectDispatcherCallback = base::OnceCallback<void()>;

  // Registers the camera HAL client to camera HAL dispatcher.
  void RegisterClient(mojom::CameraHalClientPtr camera_hal_client);

  void RegisterClientOnThread(mojom::CameraHalClientPtr camera_hal_client);

  void InitOnThread(IntOnceCallback init_callback);

  void OnDispatcherError();

  base::Thread ipc_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  mojom::CameraHalDispatcherPtr dispatcher_;
  std::unique_ptr<CameraClient> camera_client_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_SERVICE_CONNECTOR_IMPL_H_
