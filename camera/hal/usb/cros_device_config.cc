/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/cros_device_config.h"

#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>

#include "cros-camera/common.h"

namespace cros {

namespace {

constexpr char kCrosConfigCameraPath[] = "/camera";
constexpr char kCrosConfigLegacyUsbKey[] = "legacy-usb";

}  // namespace

std::unique_ptr<CrosDeviceConfig> CrosDeviceConfig::Create() {
  CrosDeviceConfig res = {};
  brillo::CrosConfig cros_config;

  if (!cros_config.Init()) {
    LOGF(ERROR) << "Failed to initialize CrOS config";
    return nullptr;
  }

  if (!cros_config.GetString("/", "name", &res.model_name)) {
    LOGF(ERROR) << "Failed to get model name of CrOS device";
    return nullptr;
  }

  std::string use_legacy_usb;
  if (cros_config.GetString(kCrosConfigCameraPath, kCrosConfigLegacyUsbKey,
                            &use_legacy_usb)) {
    if (use_legacy_usb == "true") {
      LOGF(INFO) << "The CrOS device is marked to have v1 camera devices";
    }
    res.is_v1_device = use_legacy_usb == "true";
  } else {
    res.is_v1_device = false;
  }

  // Get USB camera count from "count" and "devices" array in cros_config.
  // TODO(kamesan): Use the ids, facing, orientation in cros_config to identify
  // cameras and their layout.
  res.usb_camera_count = [&]() -> base::Optional<int> {
    // The "count" includes both MIPI and USB cameras, so we only know there's
    // no USB camera when it's zero.
    std::string count_str;
    if (cros_config.GetString("/camera", "count", &count_str)) {
      if (count_str == "0") {
        return 0;
      }
    }
    int count = 0;
    for (int i = 0;; ++i) {
      std::string interface;
      if (!cros_config.GetString(base::StringPrintf("/camera/devices/%i", i),
                                 "interface", &interface)) {
        if (i == 0) {
          // The "devices" array may be empty because there's no camera or
          // the config is not provided, so we get no information in this case.
          return base::nullopt;
        }
        break;
      }
      if (interface == "usb") {
        ++count;
      }
    }
    return count;
  }();

  return std::make_unique<CrosDeviceConfig>(res);
}

}  // namespace cros
