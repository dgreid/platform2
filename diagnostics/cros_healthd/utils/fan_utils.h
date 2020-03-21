// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_FAN_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_FAN_UTILS_H_

#include <vector>

#include <base/macros.h>

#include "debugd/dbus-proxies.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The FanFetcher class is responsible for gathering fan info reported by
// cros_healthd.
class FanFetcher {
 public:
  explicit FanFetcher(org::chromium::debugdProxyInterface* debugd_proxy);
  ~FanFetcher();

  // Returns information about each of the device's fans.
  std::vector<chromeos::cros_healthd::mojom::FanInfoPtr> FetchFanInfo();

 private:
  // Unowned pointer that outlives this FanFetcher instance.
  org::chromium::debugdProxyInterface* debugd_proxy_;

  DISALLOW_COPY_AND_ASSIGN(FanFetcher);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_FAN_UTILS_H_
