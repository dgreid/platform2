/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_

#include <memory>

#include <base/time/time.h>
#include "cros-camera/export.h"

namespace cros {

enum class JpegProcessType { kDecode, kEncode };

enum class JpegProcessMethod { kHardware, kSoftware };

class CROS_CAMERA_EXPORT CameraMetrics {
 public:
  static std::unique_ptr<CameraMetrics> New();

  virtual ~CameraMetrics() = default;

  // Records the process time of JDA/JEA in microseconds.
  virtual void SendJpegProcessLatency(JpegProcessType process_type,
                                      JpegProcessMethod process_layer,
                                      base::TimeDelta latency) = 0;

  // Records the resolution of image that JDA/JEA process in pixels.
  virtual void SendJpegResolution(JpegProcessType process_type,
                                  JpegProcessMethod process_layer,
                                  int width,
                                  int height) = 0;

  // Records the process time of ConfigureStreams().
  virtual void SendConfigureStreamsLatency(base::TimeDelta latency) = 0;

  // Records the resolution of streams that configured.
  virtual void SendConfigureStreamResolution(int width,
                                             int height,
                                             int format) = 0;

  // Records the type of the client that called OpenDevice().
  virtual void SendOpenDeviceClientType(int client_type) = 0;

  // Records the process time of OpenDevice().
  virtual void SendOpenDeviceLatency(base::TimeDelta latency) = 0;

  // Records the error type which triggers Notify().
  virtual void SendError(int error_code) = 0;

  // Records the camera facing of current session.
  virtual void SendCameraFacing(int facing) = 0;

  // Records the duration of the closing session.
  virtual void SendSessionDuration(base::TimeDelta duration) = 0;
};

}  // namespace cros

#endif  // CAMERA_INCLUDE_CROS_CAMERA_CAMERA_METRICS_H_
