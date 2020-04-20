// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_H_

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Interface which allows clients to subscribe to power-related events.
class PowerEvents {
 public:
  virtual ~PowerEvents() = default;

  // Adds a new observer to be notified when power-related events occur.
  virtual void AddObserver(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_H_
