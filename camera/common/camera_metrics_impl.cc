/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_metrics_impl.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <hardware/camera3.h>
#include <system/graphics.h>

#include "cros-camera/common.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

namespace {

constexpr char kCameraJpegProcessLatency[] =
    "ChromeOS.Camera.Jpeg.Latency.%s.%s";
constexpr base::TimeDelta kMinLatency = base::TimeDelta::FromMicroseconds(1);
constexpr base::TimeDelta kMaxLatency = base::TimeDelta::FromSeconds(1);
constexpr int kBucketLatency = 100;

constexpr char kCameraJpegResolution[] =
    "ChromeOS.Camera.Jpeg.Resolution.%s.%s";
constexpr int kMinResolutionInPixels = 1;
constexpr int kMaxResolutionInPixels = 15000000;  // 15 MegaPixels.
constexpr int kBucketResolutionInPixels = 50;

constexpr char kCameraConfigureStreamsLatency[] =
    "ChromeOS.Camera.ConfigureStreamsLatency";

constexpr char kCameraConfigureStreamsResolution[] =
    "ChromeOS.Camera.ConfigureStreams.Output.Resolution.%s";

constexpr char kCameraOpenDeviceClientType[] =
    "ChromeOS.Camera.OpenDeviceClientType";
constexpr char kNumClientTypes =
    static_cast<int32_t>(mojom::CameraClientType::kMaxValue) + 1;

constexpr char kCameraOpenDeviceLatency[] = "ChromeOS.Camera.OpenDeviceLatency";

constexpr char kCameraErrorType[] = "ChromeOS.Camera.ErrorType";

constexpr char kCameraFacing[] = "ChromeOS.Camera.Facing";
// Includes CAMERA_FACING_BACK, CAMERA_FACING_FRONT and CAMERA_FACING_EXTERNAL.
constexpr int kNumCameraFacings = 3;

constexpr char kCameraSessionDuration[] = "ChromeOS.Camera.SessionDuration";
constexpr base::TimeDelta kMinCameraSessionDuration =
    base::TimeDelta::FromSeconds(1);
constexpr base::TimeDelta kMaxCameraSessionDuration =
    base::TimeDelta::FromDays(1);
constexpr int kBucketCameraSessionDuration = 100;

}  // namespace

// static
std::unique_ptr<CameraMetrics> CameraMetrics::New() {
  return std::make_unique<CameraMetricsImpl>();
}

CameraMetricsImpl::CameraMetricsImpl()
    : metrics_lib_(std::make_unique<MetricsLibrary>()) {}

CameraMetricsImpl::~CameraMetricsImpl() = default;

void CameraMetricsImpl::SetMetricsLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> metrics_lib) {
  metrics_lib_ = std::move(metrics_lib);
}

void CameraMetricsImpl::SendJpegProcessLatency(JpegProcessType process_type,
                                               JpegProcessMethod process_layer,
                                               base::TimeDelta latency) {
  std::string action_name = base::StringPrintf(
      kCameraJpegProcessLatency,
      (process_layer == JpegProcessMethod::kHardware ? "Hardware" : "Software"),
      (process_type == JpegProcessType::kDecode ? "Decode" : "Encode"));
  metrics_lib_->SendToUMA(action_name, latency.InMicroseconds(),
                          kMinLatency.InMicroseconds(),
                          kMaxLatency.InMicroseconds(), kBucketLatency);
}

void CameraMetricsImpl::SendJpegResolution(JpegProcessType process_type,
                                           JpegProcessMethod process_layer,
                                           int width,
                                           int height) {
  std::string action_name = base::StringPrintf(
      kCameraJpegResolution,
      (process_layer == JpegProcessMethod::kHardware ? "Hardware" : "Software"),
      (process_type == JpegProcessType::kDecode ? "Decode" : "Encode"));
  metrics_lib_->SendToUMA(action_name, width * height, kMinResolutionInPixels,
                          kMaxResolutionInPixels, kBucketResolutionInPixels);
}

void CameraMetricsImpl::SendConfigureStreamsLatency(base::TimeDelta latency) {
  metrics_lib_->SendToUMA(kCameraConfigureStreamsLatency,
                          latency.InMicroseconds(),
                          kMinLatency.InMicroseconds(),
                          kMaxLatency.InMicroseconds(), kBucketLatency);
}

void CameraMetricsImpl::SendConfigureStreamResolution(int width,
                                                      int height,
                                                      int format) {
  std::string format_str;
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
      format_str = "RGBA_8888";
      break;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      format_str = "RGBX_8888";
      break;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      format_str = "BGRA_8888";
      break;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      format_str = "YCrCb_420_SP";
      break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
      format_str = "YCbCr_422_I";
      break;
    case HAL_PIXEL_FORMAT_BLOB:
      format_str = "BLOB";
      break;
    case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      format_str = "IMPLEMENTATION_DEFINED";
      break;
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
      format_str = "YCbCr_420_888";
      break;
    case HAL_PIXEL_FORMAT_YV12:
      format_str = "YV12";
      break;
  }
  std::string action_name =
      base::StringPrintf(kCameraConfigureStreamsResolution, format_str.c_str());
  metrics_lib_->SendToUMA(action_name, width * height, kMinResolutionInPixels,
                          kMaxResolutionInPixels, kBucketResolutionInPixels);
}

void CameraMetricsImpl::SendOpenDeviceClientType(int client_type) {
  metrics_lib_->SendEnumToUMA(kCameraOpenDeviceClientType, client_type,
                              kNumClientTypes);
}

void CameraMetricsImpl::SendOpenDeviceLatency(base::TimeDelta latency) {
  metrics_lib_->SendToUMA(kCameraOpenDeviceLatency, latency.InMicroseconds(),
                          kMinLatency.InMicroseconds(),
                          kMaxLatency.InMicroseconds(), kBucketLatency);
}

void CameraMetricsImpl::SendError(int error_code) {
  metrics_lib_->SendEnumToUMA(kCameraErrorType, error_code,
                              CAMERA3_MSG_NUM_ERRORS);
}

void CameraMetricsImpl::SendCameraFacing(int facing) {
  metrics_lib_->SendEnumToUMA(kCameraFacing, facing, kNumCameraFacings);
}

void CameraMetricsImpl::SendSessionDuration(base::TimeDelta duration) {
  metrics_lib_->SendToUMA(kCameraSessionDuration, duration.InSeconds(),
                          kMinCameraSessionDuration.InSeconds(),
                          kMaxCameraSessionDuration.InSeconds(),
                          kBucketCameraSessionDuration);
}

}  // namespace cros
