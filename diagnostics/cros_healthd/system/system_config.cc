// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/system_config.h"

#include <string>

#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <base/files/file_util.h>

#include "diagnostics/cros_healthd/system/system_config_constants.h"

namespace diagnostics {

SystemConfig::SystemConfig(brillo::CrosConfigInterface* cros_config)
    : cros_config_(cros_config), root_dir_("/") {}

SystemConfig::SystemConfig(brillo::CrosConfigInterface* cros_config,
                           const base::FilePath& root_dir)
    : cros_config_(cros_config), root_dir_(root_dir) {}

SystemConfig::~SystemConfig() = default;

bool SystemConfig::FioSupported() {
  return base::PathExists(root_dir_.AppendASCII(kFioToolPath));
}

bool SystemConfig::HasBacklight() {
  std::string has_backlight;
  cros_config_->GetString(kBacklightPropertiesPath, kHasBacklightProperty,
                          &has_backlight);
  return has_backlight != "false";
}

bool SystemConfig::HasBattery() {
  std::string psu_type;
  cros_config_->GetString(kHardwarePropertiesPath, kPsuTypeProperty, &psu_type);
  return psu_type != "AC_only";
}

bool SystemConfig::HasSkuNumberProperty() {
  std::string has_sku_number;
  cros_config_->GetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                          &has_sku_number);
  return has_sku_number == "true";
}

bool SystemConfig::HasSmartBattery() {
  std::string has_smart_battery_info;
  cros_config_->GetString(kBatteryPropertiesPath, kHasSmartBatteryInfoProperty,
                          &has_smart_battery_info);
  return has_smart_battery_info == "true";
}

bool SystemConfig::NvmeSupported() {
  return base::PathExists(root_dir_.AppendASCII(kNvmeToolPath));
}

bool SystemConfig::SmartCtlSupported() {
  return base::PathExists(root_dir_.AppendASCII(kSmartctlToolPath));
}

}  // namespace diagnostics
