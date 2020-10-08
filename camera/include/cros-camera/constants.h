/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_INCLUDE_CROS_CAMERA_CONSTANTS_H_
#define CAMERA_INCLUDE_CROS_CAMERA_CONSTANTS_H_

namespace cros {

namespace constants {

const char kArcCameraGroup[] = "arc-camera";
const char kCrosCameraAlgoSocketPathString[] = "/run/camera/camera-algo.sock";
const char kCrosCameraGPUAlgoSocketPathString[] =
    "/run/camera/camera-gpu-algo.sock";
const char kCrosCameraSocketPathString[] = "/run/camera/camera3.sock";
const char kCrosCameraTestConfigPathString[] =
    "/var/cache/camera/test_config.json";
const char kCrosCameraConfigPathString[] = "/run/camera/camera_config.json";

// ------Configuration for |kCrosCameraTestConfigPathString|-------
// boolean value used in test mode for forcing hardware jpeg encode/decode in
// USB HAL (won't fallback to SW encode/decode).
const char kCrosForceJpegHardwareEncodeOption[] = "force_jpeg_hw_enc";
const char kCrosForceJpegHardwareDecodeOption[] = "force_jpeg_hw_dec";

// boolean value for specify enable/disable camera of target facing in camera
// service.
const char kCrosEnableFrontCameraOption[] = "enable_front_camera";
const char kCrosEnableBackCameraOption[] = "enable_back_camera";
const char kCrosEnableExternalCameraOption[] = "enable_external_camera";
// ------End configuration for |kCrosCameraTestConfigPathString|-------

// ------Configuration for |kCrosCameraConfigPathString|-------
// Restrict max resolutions for android hal formats.
// HAL_PIXEL_FORMAT_BLOB
const char kCrosMaxBlobWidth[] = "max_blob_width";
const char kCrosMaxBlobHeight[] = "max_blob_height";
// HAL_PIXEL_FORMAT_YCbCr_420_888
const char kCrosMaxYuvWidth[] = "max_yuv_width";
const char kCrosMaxYuvHeight[] = "max_yuv_height";
// HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
const char kCrosMaxPrivateWidth[] = "max_private_width";
const char kCrosMaxPrivateHeight[] = "max_private_height";
// Restrict max resolutions for native ratio.
const char kCrosMaxNativeWidth[] = "max_native_width";
const char kCrosMaxNativeHeight[] = "max_native_height";
// Filtered out resolutions. The format is a list string of resolutions. e.g.
// ["w1xh1", "w2xh2"]
const char kCrosFilteredOutResolutions[] = "filtered_out_resolutions";
// ------End configuration for |kCrosCameraConfigPathString|-------

}  // namespace constants
}  // namespace cros
#endif  // CAMERA_INCLUDE_CROS_CAMERA_CONSTANTS_H_
