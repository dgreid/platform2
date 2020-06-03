// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_LID_SUBSCRIBER_H_
#define DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_LID_SUBSCRIBER_H_

#include <mojo/public/cpp/bindings/binding.h>

#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// This class subscribes to cros_healthd's lid notifications and outputs any
// notifications received to stdout.
class LidSubscriber final
    : public chromeos::cros_healthd::mojom::CrosHealthdLidObserver {
 public:
  explicit LidSubscriber(
      chromeos::cros_healthd::mojom::CrosHealthdLidObserverRequest request);
  LidSubscriber(const LidSubscriber&) = delete;
  LidSubscriber& operator=(const LidSubscriber&) = delete;
  ~LidSubscriber();

  // chromeos::cros_healthd::mojom::CrosHealthdLidObserver overrides:
  void OnLidClosed() override;
  void OnLidOpened() override;

 private:
  // Allows the remote cros_healthd to call LidSubscriber's
  // CrosHealthdLidObserver methods.
  const mojo::Binding<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
      binding_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTH_TOOL_EVENT_LID_SUBSCRIBER_H_
