/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
#define CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_

#include <memory>
#include <string>

#include <base/optional.h>
#include <chromeos-config/libcros_config/cros_config.h>

namespace cros {

// This structs wraps the brillo::CrosConfig and stores the required values.
class CrosDeviceConfig {
 public:
  static std::unique_ptr<CrosDeviceConfig> Create();

  bool IsV1Device() const { return is_v1_device; }
  const std::string& GetModelName() const { return model_name; }
  bool IsUsbCameraCountAvailable() const {
    return usb_camera_count.has_value();
  }
  int GetUsbCameraCount() const { return *usb_camera_count; }

 private:
  CrosDeviceConfig() = default;

  bool is_v1_device;
  std::string model_name;
  base::Optional<int> usb_camera_count;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
