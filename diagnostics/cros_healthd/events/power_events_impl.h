// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_IMPL_H_

#include <base/optional.h>
#include <mojo/public/cpp/bindings/interface_ptr_set.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

#include "diagnostics/cros_healthd/events/power_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Production implementation of the PowerEvents interface.
class PowerEventsImpl final : public PowerEvents,
                              public PowerdAdapter::PowerObserver {
 public:
  explicit PowerEventsImpl(Context* context);
  PowerEventsImpl(const PowerEventsImpl&) = delete;
  PowerEventsImpl& operator=(const PowerEventsImpl&) = delete;
  ~PowerEventsImpl() override;

  // PowerEvents overrides:
  void AddObserver(chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr
                       observer) override;

 private:
  // Mapping between powerd's PowerSupplyProperties and events that PowerEvents
  // cares about.
  enum class PowerEventType {
    // Energy consumption from an external power source has started.
    kAcInserted,
    // Energy consumption from an external power source has stopped.
    kAcRemoved,
  };

  // PowerdAdapter::Observer overrides:
  void OnPowerSupplyPollSignal(
      const power_manager::PowerSupplyProperties& power_supply) override;
  void OnSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) override;
  void OnDarkSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) override;
  void OnSuspendDoneSignal(
      const power_manager::SuspendDone& suspend_done) override;

  // Common response to either a SuspendImminentSignal or
  // DarkSuspendImminentSignal.
  void OnAnySuspendImminentSignal();

  // Checks to see if any observers are left. If not, removes this object from
  // powerd's observers.
  void StopObservingPowerdIfNecessary();

  // Tracks whether or not this instance has added itself as an observer of
  // powerd.
  bool is_observing_powerd_ = false;

  // Most recent external power AC event, from powerd's last
  // PowerSupplyPollSignal (updates every 30 seconds or when something changes
  // in the power supply).
  base::Optional<PowerEventType> external_power_ac_event_;

  // Each observer in |observers_| will be notified of any power event in the
  // chromeos::cros_healthd::mojom::CrosHealthdPowerObserver interface. The
  // InterfacePtrSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::InterfacePtrSet<chromeos::cros_healthd::mojom::CrosHealthdPowerObserver>
      observers_;

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_POWER_EVENTS_IMPL_H_
