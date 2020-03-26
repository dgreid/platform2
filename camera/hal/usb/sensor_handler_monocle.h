/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_SENSOR_HANDLER_MONOCLE_H_
#define CAMERA_HAL_USB_SENSOR_HANDLER_MONOCLE_H_

#include <map>
#include <vector>

#include "hal/usb/common_types.h"
#include "hal/usb/sensor_handler.h"

namespace cros {

class SensorHandlerMonocle : public SensorHandler {
 public:
  // We need some information from sensor to calculate rolling shutter skew
  // metadata. The metadata is different for each resolution.
  struct SensorParameters {
    // The number of pixels horizontally.
    int64_t line_pixel_width;
    // The line number vertically.
    int64_t line_count;

    // Cache calculated rolling shutter skew.
    int64_t readout_time;
    // Cache calculated line duration for exposure time.
    int64_t line_duration;
  };

  SensorHandlerMonocle(const DeviceInfo& device_info,
                       const SupportedFormats& supported_formats);
  ~SensorHandlerMonocle() override;

  int64_t GetRollingShutterSkew(const Size& resolution) override;
  int64_t GetExposureTime(const Size& resolution) override;

 private:
  // Initial sensor parameters for all resolutions by device info.
  void InitSensorParameters(const DeviceInfo& device_info,
                            const SupportedFormats& supported_formats);

  // Clock rate used in camera sensor. The unit is HZ.
  static constexpr int64_t kPixelClock_ = 144'000'000;

  // Exposure time parameters.
  static constexpr uint32_t kExposureTimeFractionBits_ = 4;

  // Sensor parameters for each resolution.
  std::map<Size, SensorParameters> sensor_parameters_;

  // file handle for third_party SDK to read sensor data.
  void* handle_;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_SENSOR_HANDLER_MONOCLE_H_
