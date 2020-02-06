// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <dbus/object_proxy.h>

#include "debugd/dbus-proxies.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BatteryFetcher class is responsible for gathering battery info reported
// by cros_healthd. Some info is fetched via powerd, while Smart Battery info
// is collected from ectool via debugd.
class BatteryFetcher {
 public:
  BatteryFetcher(org::chromium::debugdProxyInterface* debugd_proxy,
                 dbus::ObjectProxy* power_manager_proxy,
                 brillo::CrosConfigInterface* cros_config);
  ~BatteryFetcher();

  // Fetches a device's battery info.
  chromeos::cros_healthd::mojom::BatteryInfoPtr FetchBatteryInfo();

 private:
  // Populates |info| with battery info from |response|. Returns true on
  // success.
  bool GetBatteryInfoFromPowerdResponse(
      dbus::Response* response,
      chromeos::cros_healthd::mojom::BatteryInfo* info);

  // Populates |smart_info| with Smart Battery info obtained by using ectool via
  // debugd.
  void GetSmartBatteryInfo(
      chromeos::cros_healthd::mojom::SmartBatteryInfo* smart_info);

  // Populates |metric_value| with the value obtained from requesting
  // |metric_name| from ectool via debugd. Returns true on success.
  template <typename T>
  bool GetSmartBatteryMetric(
      const std::string& metric_name,
      base::OnceCallback<bool(const base::StringPiece& input, T* output)>
          convert_string_to_num,
      T* metric_value);

  // Returns true if the device's config indicates the device has a battery.
  bool HasBattery();

  // Returns true if the device's config indicates the device has Smart Battery
  // info.
  bool HasSmartBatteryInfo();

  // Unowned pointer that outlives this BatteryFetcher instance.
  org::chromium::debugdProxyInterface* debugd_proxy_;

  // Unowned pointer that outlives this BatteryFetcher instance.
  dbus::ObjectProxy* power_manager_proxy_;

  // Unowned pointer that outlives this BatteryFetcher instance.
  brillo::CrosConfigInterface* cros_config_;

  DISALLOW_COPY_AND_ASSIGN(BatteryFetcher);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
