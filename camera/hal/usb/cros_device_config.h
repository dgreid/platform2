/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
#define CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_

#include <string>

#include <base/optional.h>
#include <chromeos-config/libcros_config/cros_config.h>

namespace cros {

// This structs wraps the brillo::CrosConfig and stores the required values.
struct CrosDeviceConfig {
 public:
  CrosDeviceConfig();
  CrosDeviceConfig(const CrosDeviceConfig& other) = default;
  CrosDeviceConfig& operator=(const CrosDeviceConfig& other) = default;
  ~CrosDeviceConfig();

  static CrosDeviceConfig Get();

  bool is_initialized;
  bool is_v1_device;
  std::string model_name;
  base::Optional<int> usb_camera_count;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_CROS_DEVICE_CONFIG_H_
