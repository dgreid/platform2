/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/callback_forward.h>
#include <base/memory/ref_counted.h>

#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "mojo/algorithm/camera_algorithm.mojom.h"
#include "mojo/cros_camera_service.mojom.h"
#include "mojo/gpu/jpeg_encode_accelerator.mojom.h"
#include "mojo/gpu/mjpeg_decode_accelerator.mojom.h"

namespace base {
class SingleThreadTaskRunner;
}

namespace cros {

// There are many places that need to initialize Mojo and use related channels.
// This class is used to manage them together.
class CROS_CAMERA_EXPORT CameraMojoChannelManager
    : public CameraMojoChannelManagerToken {
 public:
  using Callback = base::OnceCallback<void(void)>;

  virtual ~CameraMojoChannelManager() {}

  // TODO(b/151270948): Remove this method once all camera HALs implement
  // the CrOS specific interface so that we can pass the mojo manager instance
  // to them.
  static CameraMojoChannelManager* GetInstance();

  static CameraMojoChannelManager* FromToken(
      CameraMojoChannelManagerToken* token) {
    return static_cast<CameraMojoChannelManager*>(token);
  }

  // Gets the task runner that the CameraHalDispatcher interface is bound to.
  virtual scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner() = 0;

  // Registers the camera HAL server pointer |hal_ptr| to the
  // CameraHalDispatcher.
  // This method is expected to be called on the IPC thread and the
  // |on_construct_callback| and |on_error_callback| will be run on the IPC
  // thread as well.
  virtual void RegisterServer(
      mojom::CameraHalServerPtr hal_ptr,
      mojom::CameraHalDispatcher::RegisterServerWithTokenCallback
          on_construct_callback,
      Callback on_error_callback) = 0;

  // Creates a new MjpegDecodeAccelerator connection by |request|.
  // This method is expected to be called on the IPC thread and the
  // |on_construct_callback| and |on_error_callback| will be run on the IPC
  // thread as well.
  virtual void CreateMjpegDecodeAccelerator(
      mojom::MjpegDecodeAcceleratorRequest request,
      Callback on_construct_callback,
      Callback on_error_callback) = 0;

  // Creates a new JpegEncodeAccelerator connection by |request|.
  // This method is expected to be called on the IPC thread and the
  // |on_construct_callback| and |on_error_callback| will be run on the IPC
  // thread as well.
  virtual void CreateJpegEncodeAccelerator(
      mojom::JpegEncodeAcceleratorRequest request,
      Callback on_construct_callback,
      Callback on_error_callback) = 0;

  // Create a new CameraAlgorithmOpsPtr.
  virtual mojom::CameraAlgorithmOpsPtr CreateCameraAlgorithmOpsPtr(
      const std::string& socket_path, const std::string& pipe_name) = 0;
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_
