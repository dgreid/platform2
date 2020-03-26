/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/sensor_handler_monocle.h"

#include <string>

#include <base/files/file_util.h>
#include <rts_read_sensor.h>

#include "cros-camera/common.h"
#include "hal/usb/quirks.h"

// The sensor registers to export exposure time.
static constexpr uint32_t kExposureTimeRegisters[] = {0x3500, 0x3501, 0x3502};

namespace cros {

SensorHandlerMonocle::SensorHandlerMonocle(
    const DeviceInfo& device_info, const SupportedFormats& supported_formats)
    : handle_(nullptr) {
  if (!(device_info.quirks & kQuirkMonocle)) {
    return;
  }

  base::FilePath target;
  std::string device_path;
  // SDK supposes the device path should be /dev/videoX.
  if (base::ReadSymbolicLink(base::FilePath(device_info.device_path),
                             &target)) {
    device_path = "/dev/" + target.BaseName().value();
  } else {
    device_path = device_info.device_path;
  }
  handle_ = rts_uvc_open(device_path.c_str());
  if (handle_ == nullptr) {
    LOGF(ERROR) << "Cannot open device to read sensor data: " << device_path;
  }

  InitSensorParameters(device_info, supported_formats);
}

SensorHandlerMonocle::~SensorHandlerMonocle() {
  if (handle_)
    rts_uvc_close(handle_);
}

void SensorHandlerMonocle::InitSensorParameters(
    const DeviceInfo& device_info, const SupportedFormats& supported_formats) {
  // TODO(henryhsu): Move to a config file.
  if (device_info.usb_vid == "0bda" && device_info.usb_pid == "5647") {
    // These parameters are from OV8856 specification.
    // Initial exposure time parameters
    SensorParameters param;
    for (const auto& supported_format : supported_formats) {
      if (supported_format.width == 3264 && supported_format.height == 2448) {
        param.line_pixel_width = 3864;
        param.line_count = 2452;
      } else if (supported_format.width == 1920 &&
                 supported_format.height == 1080) {
        param.line_pixel_width = 3200;
        param.line_count = 1840;
      } else {
        param.line_pixel_width = 3864;
        param.line_count = 1224;
      }

      param.readout_time = param.line_pixel_width * param.line_count *
                           1'000'000'000 / kPixelClock_;
      param.line_duration =
          param.line_pixel_width * 1'000'000'000 / kPixelClock_;

      Size resolution(supported_format.width, supported_format.height);
      sensor_parameters_[resolution] = param;
    }
  }
}

int64_t SensorHandlerMonocle::GetRollingShutterSkew(const Size& resolution) {
  if (sensor_parameters_.find(resolution) == sensor_parameters_.end()) {
    return 33'300'000;
  }
  return sensor_parameters_[resolution].readout_time;
}

int64_t SensorHandlerMonocle::GetExposureTime(const Size& resolution) {
  if (handle_ == nullptr ||
      sensor_parameters_.find(resolution) == sensor_parameters_.end()) {
    return 16'600'000;
  }
  uint64_t line_count = 0;
  for (const auto& addr : kExposureTimeRegisters) {
    uint16_t value;
    rts_read_sensor_reg(handle_, addr, &value);
    line_count = (line_count << 8) | value;
  }
  line_count >>= kExposureTimeFractionBits_;
  return sensor_parameters_[resolution].line_duration * line_count;
}

}  // namespace cros
