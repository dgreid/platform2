/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_

#include <memory>
#include <string>

#include <base/callback_forward.h>
#include <base/memory/ref_counted.h>

#include "cros-camera/export.h"
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
class CROS_CAMERA_EXPORT CameraMojoChannelManager {
 public:
  static std::unique_ptr<CameraMojoChannelManager> CreateInstance();

  virtual ~CameraMojoChannelManager() {}

  // Connects to the CameraHalDispatcher.  When the Mojo connection is
  // established successfully, |on_connection_established| will be called and
  // |on_connection_error| is set as the Mojo connection error handler.
  virtual void ConnectToDispatcher(base::Closure on_connection_established,
                                   base::Closure on_connection_error) = 0;

  // Gets the task runner that the CameraHalDispatcher interface is bound to.
  virtual scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner() = 0;

  // Registers the camera HAL server to the CameraHalDispatcher.
  virtual void RegisterServer(mojom::CameraHalServerPtr hal_ptr) = 0;

  // Creates a new MjpegDecodeAccelerator.
  virtual bool CreateMjpegDecodeAccelerator(
      mojom::MjpegDecodeAcceleratorRequest request) = 0;

  // Creates a new JpegEncodeAccelerator.
  virtual bool CreateJpegEncodeAccelerator(
      mojom::JpegEncodeAcceleratorRequest request) = 0;

  // Create a new CameraAlgorithmOpsPtr.
  virtual mojom::CameraAlgorithmOpsPtr CreateCameraAlgorithmOpsPtr(
      const std::string& socket_path, const std::string& pipe_name) = 0;
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_MOJO_CHANNEL_MANAGER_H_
