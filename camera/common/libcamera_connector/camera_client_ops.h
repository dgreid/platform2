/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_OPS_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_OPS_H_

#include <vector>

#include <base/callback.h>
#include <base/containers/flat_map.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/stream_buffer_manager.h"
#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_service_connector.h"
#include "mojo/camera3.mojom.h"

namespace cros {

class CameraClientOps : public mojom::Camera3CallbackOps {
 public:
  static const int kStreamId = 0;

  using DeviceOpsInitCallback =
      base::OnceCallback<void(mojom::Camera3DeviceOpsRequest)>;
  using CaptureResultCallback =
      base::Callback<void(const cros_cam_capture_result_t&)>;

  CameraClientOps();
  ~CameraClientOps();

  void Init(DeviceOpsInitCallback init_callback,
            CaptureResultCallback result_callback);

  void StartCapture(int32_t camera_id,
                    const cros_cam_format_info_t* format,
                    int32_t jpeg_max_size);

  void StopCapture(mojom::Camera3DeviceOps::CloseCallback close_callback);

  // ProcessCaptureResult is an implementation of ProcessCaptureResult in
  // Camera3CallbackOps. It receives the result metadata and filled buffers from
  // the camera service.
  void ProcessCaptureResult(mojom::Camera3CaptureResultPtr result) override;

  // Notify is an implementation of Notify in Camera3CallbackOps. It receives
  // shutter messages and error notifications.
  void Notify(mojom::Camera3NotifyMsgPtr msg) override;

 private:
  void InitOnThread(DeviceOpsInitCallback init_callback,
                    CaptureResultCallback result_callback);

  void StartCaptureOnThread(int32_t camera_id,
                            const cros_cam_format_info_t* format,
                            int32_t jpeg_max_size);

  void StopCaptureOnThread(
      mojom::Camera3DeviceOps::CloseCallback close_callback);

  void InitializeDevice();

  void OnInitializedDevice(int32_t result);

  void ConfigureStreams();

  void OnConfiguredStreams(
      int32_t result,
      mojom::Camera3StreamConfigurationPtr updated_config,
      base::flat_map<uint64_t, std::vector<mojom::Camera3StreamBufferPtr>>
          allocated_buffers);

  void ConstructDefaultRequestSettings();

  void OnConstructedDefaultRequestSettings(mojom::CameraMetadataPtr settings);

  void ConstructCaptureRequest();

  void ConstructCaptureRequestOnThread();

  void ProcessCaptureRequestOnThread(mojom::Camera3CaptureRequestPtr request);

  void OnProcessedCaptureRequest(int32_t result);

  void SendCaptureResult(const cros_cam_capture_result_t& result);

  base::Thread ops_thread_;

  mojom::Camera3DeviceOpsPtr device_ops_;
  mojo::Binding<mojom::Camera3CallbackOps> camera3_callback_ops_;

  CaptureResultCallback result_callback_;
  bool capture_started_;

  int32_t request_camera_id_;
  cros_cam_format_info_t request_format_;
  int32_t jpeg_max_size_;

  StreamBufferManager buffer_manager_;
  mojom::Camera3StreamConfigurationPtr stream_config_;
  mojom::CameraMetadataPtr request_settings_;

  uint32_t frame_number_;
  base::Lock frame_number_lock_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_OPS_H_
