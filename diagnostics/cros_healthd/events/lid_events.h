// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_H_

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Interface which allows clients to subscribe to lid-related events.
class LidEvents {
 public:
  virtual ~LidEvents() = default;

  // Adds a new observer to be notified when lid-related events occur.
  virtual void AddObserver(
      chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_H_
