/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_METRICS_IMPL_H_
#define CAMERA_COMMON_CAMERA_METRICS_IMPL_H_

#include <memory>

#include <base/time/time.h>
#include <base/process/process_metrics.h>
#include <cros-camera/camera_metrics.h>
#include <metrics/metrics_library.h>

namespace cros {

// Implementation of Camera Metrics.
class CameraMetricsImpl : public CameraMetrics {
 public:
  CameraMetricsImpl();
  ~CameraMetricsImpl() override;

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_lib);

  void SendJpegProcessLatency(JpegProcessType process_type,
                              JpegProcessMethod process_layer,
                              base::TimeDelta latency) override;
  void SendJpegResolution(JpegProcessType process_type,
                          JpegProcessMethod process_layer,
                          int width,
                          int height) override;
  void SendConfigureStreamResolution(int width,
                                     int height,
                                     int format) override;
  void SendConfigureStreamsLatency(base::TimeDelta latency) override;
  void SendOpenDeviceClientType(int client_type) override;
  void SendOpenDeviceLatency(base::TimeDelta latency) override;
  void SendError(int error_code) override;
  void SendCameraFacing(int facing) override;
  void SendSessionDuration(base::TimeDelta duration) override;

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_lib_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_METRICS_IMPL_H_
