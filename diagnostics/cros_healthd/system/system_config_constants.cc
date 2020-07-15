// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/system_config_constants.h"

namespace diagnostics {

const char kHardwarePropertiesPath[] = "/hardware-properties";
const char kPsuTypeProperty[] = "psu-type";
const char kBatteryPropertiesPath[] = "/cros-healthd/battery";
const char kHasSmartBatteryInfoProperty[] = "has-smart-battery-info";
const char kBacklightPropertiesPath[] = "/cros-healthd/backlight";
const char kHasBacklightProperty[] = "has-backlight";
const char kCachedVpdPropertiesPath[] = "/cros-healthd/cached-vpd";
const char kHasSkuNumberProperty[] = "has-sku-number";
const char kArcBuildPropertiesPath[] = "/arc/build-properties";
const char kMarketingNameProperty[] = "marketing-name";

const char kNvmeToolPath[] = "usr/sbin/nvme";
const char kSmartctlToolPath[] = "usr/sbin/smartctl";
const char kFioToolPath[] = "usr/bin/fio";

}  // namespace diagnostics
