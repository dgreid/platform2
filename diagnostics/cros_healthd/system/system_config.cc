// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/system_config.h"

#include <algorithm>
#include <string>

#include <chromeos/chromeos-config/libcros_config/cros_config.h>
#include <base/files/file_util.h>
#include <base/system/sys_info.h>

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
  // Assume that device has a backlight unless otherwise configured.
  if (!cros_config_->GetString(kHardwarePropertiesPath, kHasBacklightProperty,
                               &has_backlight)) {
    return true;
  }
  return has_backlight != "false";
}

bool SystemConfig::HasBattery() {
  std::string psu_type;
  // Assume that device has a battery unless otherwise configured.
  if (!cros_config_->GetString(kHardwarePropertiesPath, kPsuTypeProperty,
                               &psu_type)) {
    return true;
  }
  return psu_type != "AC_only";
}

bool SystemConfig::HasSkuNumber() {
  std::string has_sku_number;
  // Assume that device have does NOT have a SKU number unless otherwise
  // configured.
  if (!cros_config_->GetString(kCachedVpdPropertiesPath, kHasSkuNumberProperty,
                               &has_sku_number)) {
    return false;
  }
  return has_sku_number == "true";
}

bool SystemConfig::HasSmartBattery() {
  std::string has_smart_battery_info;
  // Assume that device does NOT have a smart battery unless otherwise
  // configured.
  if (!cros_config_->GetString(kBatteryPropertiesPath,
                               kHasSmartBatteryInfoProperty,
                               &has_smart_battery_info)) {
    return false;
  }
  return has_smart_battery_info == "true";
}

bool SystemConfig::NvmeSupported() {
  return base::PathExists(root_dir_.AppendASCII(kNvmeToolPath));
}

bool SystemConfig::SmartCtlSupported() {
  return base::PathExists(root_dir_.AppendASCII(kSmartctlToolPath));
}

bool SystemConfig::IsWilcoDevice() {
  const auto wilco_devices = GetWilcoBoardNames();
  return std::count(wilco_devices.begin(), wilco_devices.end(),
                    base::SysInfo::GetLsbReleaseBoard());
}

std::string SystemConfig::GetMarketingName() {
  std::string marketing_name;
  // Assume that device does NOT have a marketing name unless otherwise
  // configured.
  if (!cros_config_->GetString(kArcBuildPropertiesPath, kMarketingNameProperty,
                               &marketing_name)) {
    return "";
  }
  return marketing_name;
}

}  // namespace diagnostics
