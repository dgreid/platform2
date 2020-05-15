// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_

#include <string>
#include <vector>

#include <base/optional.h>

#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BatteryFetcher class is responsible for gathering battery info reported
// by cros_healthd. Some info is fetched via powerd, while Smart Battery info
// is collected from ectool via debugd.
class BatteryFetcher {
 public:
  explicit BatteryFetcher(Context* context);
  BatteryFetcher(const BatteryFetcher&) = delete;
  BatteryFetcher& operator=(const BatteryFetcher&) = delete;
  ~BatteryFetcher();

  // Returns a structure with either the device's battery info or the error that
  // occurred fetching the information.
  chromeos::cros_healthd::mojom::BatteryResultPtr FetchBatteryInfo();

 private:
  using OptionalProbeErrorPtr =
      base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>;

  // Populates general battery data fields in |info| obtained from a
  // powerd |response|. Returns base::nullopt on success or a ProbeError
  // on failure.
  OptionalProbeErrorPtr PopulateBatteryInfoFromPowerdResponse(
      dbus::Response* response,
      chromeos::cros_healthd::mojom::BatteryInfo* info);

  // Populates the Smart Battery fields in |info| obtained by using ectool
  // via debugd. Returns base::nullopt on success or a ProbeError on failure.
  OptionalProbeErrorPtr PopulateSmartBatteryInfo(
      chromeos::cros_healthd::mojom::BatteryInfo* info);

  // Populates |metric_value| with the value obtained from requesting
  // |metric_name| from ectool via debugd. Returns base::nullopt on success or a
  // ProbeError on failure.
  template <typename T>
  OptionalProbeErrorPtr GetSmartBatteryMetric(
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
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_BATTERY_UTILS_H_
