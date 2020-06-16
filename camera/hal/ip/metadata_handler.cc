/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include "hal/ip/metadata_handler.h"
#include "hal/usb/vendor_tag.h"

namespace cros {

MetadataHandler::MetadataHandler() {}

MetadataHandler::~MetadataHandler() {}

android::CameraMetadata MetadataHandler::CreateStaticMetadata(
    const std::string& ip,
    const std::string& name,
    int format,
    int width,
    int height,
    double fps) {
  android::CameraMetadata metadata;

  std::vector<int32_t> characteristic_keys = {
      ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
      ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
      ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
      ANDROID_SENSOR_ORIENTATION,
      ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
  };

  metadata.update(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                  characteristic_keys);

  std::vector<int32_t> request_result_keys = {};
  metadata.update(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, request_result_keys);

  metadata.update(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, request_result_keys);

  std::vector<int32_t> available_fps_ranges;
  available_fps_ranges.push_back(fps);
  available_fps_ranges.push_back(fps);
  metadata.update(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                  available_fps_ranges);

  std::vector<int64_t> min_frame_durations;
  min_frame_durations.push_back(format);
  min_frame_durations.push_back(width);
  min_frame_durations.push_back(height);
  min_frame_durations.push_back(static_cast<int64_t>(1e9 / fps));

  metadata.update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                  min_frame_durations);

  std::vector<int32_t> stream_configurations;
  stream_configurations.push_back(format);
  stream_configurations.push_back(width);
  stream_configurations.push_back(height);
  stream_configurations.push_back(
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);

  metadata.update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                  stream_configurations);

  std::vector<int32_t> active_array_size = {0, 0, width, height};
  metadata.update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size);

  int32_t sensor_orientation = 0;
  metadata.update(ANDROID_SENSOR_ORIENTATION, &sensor_orientation, 1);

  const uint8_t request_pipeline_max_depth = 4;
  metadata.update(ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
                  &request_pipeline_max_depth, 1);

  metadata.update(kVendorTagDevicePath, ip);
  metadata.update(kVendorTagModelName, name);

  return metadata;
}

camera_metadata_t* MetadataHandler::GetDefaultRequestSettings() {
  static camera_metadata_t* default_metadata = allocate_camera_metadata(0, 0);
  return default_metadata;
}

}  // namespace cros
