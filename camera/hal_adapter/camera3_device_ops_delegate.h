/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA3_DEVICE_OPS_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_CAMERA3_DEVICE_OPS_DELEGATE_H_

#include <vector>

#include "hal_adapter/cros_camera_mojo_utils.h"
#include "mojo/camera3.mojom.h"

namespace cros {

class CameraDeviceAdapter;

class Camera3DeviceOpsDelegate final
    : public internal::MojoBinding<mojom::Camera3DeviceOps> {
 public:
  Camera3DeviceOpsDelegate(
      CameraDeviceAdapter* camera_device_adapter,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  ~Camera3DeviceOpsDelegate();

 private:
  void Initialize(mojom::Camera3CallbackOpsPtr callback_ops,
                  InitializeCallback callback) final;

  void ConfigureStreams(mojom::Camera3StreamConfigurationPtr config,
                        ConfigureStreamsCallback callback) final;

  void ConstructDefaultRequestSettings(
      mojom::Camera3RequestTemplate type,
      ConstructDefaultRequestSettingsCallback callback) final;

  void ProcessCaptureRequest(mojom::Camera3CaptureRequestPtr request,
                             ProcessCaptureRequestCallback callback) final;

  void Dump(mojo::ScopedHandle fd) final;

  void Flush(FlushCallback callback) final;

  void RegisterBuffer(uint64_t buffer_id,
                      mojom::Camera3DeviceOps::BufferType type,
                      std::vector<mojo::ScopedHandle> fds,
                      uint32_t drm_format,
                      mojom::HalPixelFormat hal_pixel_format,
                      uint32_t width,
                      uint32_t height,
                      const std::vector<uint32_t>& strides,
                      const std::vector<uint32_t>& offsets,
                      RegisterBufferCallback callback) final;

  void Close(CloseCallback callback) final;

  void ConfigureStreamsAndGetAllocatedBuffers(
      mojom::Camera3StreamConfigurationPtr config,
      ConfigureStreamsAndGetAllocatedBuffersCallback callback) final;

  CameraDeviceAdapter* camera_device_adapter_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3DeviceOpsDelegate);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA3_DEVICE_OPS_DELEGATE_H_
