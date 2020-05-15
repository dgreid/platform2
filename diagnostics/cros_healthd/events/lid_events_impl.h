// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_IMPL_H_

#include <mojo/public/cpp/bindings/interface_ptr_set.h>

#include "diagnostics/cros_healthd/events/lid_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Production implementation of the LidEvents interface.
class LidEventsImpl final : public LidEvents,
                            public PowerdAdapter::LidObserver {
 public:
  explicit LidEventsImpl(Context* context);
  LidEventsImpl(const LidEventsImpl&) = delete;
  LidEventsImpl& operator=(const LidEventsImpl&) = delete;
  ~LidEventsImpl() override;

  // LidEvents overrides:
  void AddObserver(chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr
                       observer) override;

 private:
  // PowerdAdapter::LidObserver overrides:
  void OnLidClosedSignal() override;
  void OnLidOpenedSignal() override;

  // Checks to see if any observers are left. If not, removes this object from
  // powerd's observers.
  void StopObservingPowerdIfNecessary();

  // Tracks whether or not this instance has added itself as an observer of
  // powerd.
  bool is_observing_powerd_ = false;

  // Each observer in |observers_| will be notified of any lid event in the
  // chromeos::cros_healthd::mojom::CrosHealthdLidObserver interface. The
  // InterfacePtrSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::InterfacePtrSet<chromeos::cros_healthd::mojom::CrosHealthdLidObserver>
      observers_;

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_LID_EVENTS_IMPL_H_
