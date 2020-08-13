// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_

namespace diagnostics {

// The path used to check a device's master configuration hardware properties.
extern const char kHardwarePropertiesPath[];
// The master configuration property that specifies a device's PSU type.
extern const char kPsuTypeProperty[];
// The path used to check a device's master configuration cros_healthd battery
// properties.
extern const char kBatteryPropertiesPath[];
// The master configuration property that indicates whether a device has Smart
// Battery info.
extern const char kHasSmartBatteryInfoProperty[];
// The master configuration property that indicates whether a device has a
// backlight.
extern const char kHasBacklightProperty[];
// The path used to check a device's master configuration cros_healthd vpd
// properties.
extern const char kCachedVpdPropertiesPath[];
// The master configuration property that indicates whether a device has a
// sku number in the VPD fields.
extern const char kHasSkuNumberProperty[];
// NVME utility program path relative to the root directory.
extern const char kNvmeToolPath[];
// Smartctl utility program path relative to the root directory.
extern const char kSmartctlToolPath[];
// Fio utility program path relative to the root directory.
extern const char kFioToolPath[];
// The path to check a device's master configuration ARC build properties.
extern const char kArcBuildPropertiesPath[];
// The master configuration property that specifies a device's marketing name.
extern const char kMarketingNameProperty[];

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_CONSTANTS_H_
