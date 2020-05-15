// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/power_events_impl.h"

#include <utility>

#include <base/logging.h>

namespace diagnostics {

PowerEventsImpl::PowerEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
}

PowerEventsImpl::~PowerEventsImpl() {
  if (is_observing_powerd_)
    context_->powerd_adapter()->RemovePowerObserver(this);
}

void PowerEventsImpl::AddObserver(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) {
  if (!is_observing_powerd_) {
    context_->powerd_adapter()->AddPowerObserver(this);
    is_observing_powerd_ = true;
  }
  observers_.AddPtr(std::move(observer));
}

void PowerEventsImpl::OnPowerSupplyPollSignal(
    const power_manager::PowerSupplyProperties& power_supply) {
  if (!power_supply.has_external_power())
    return;

  PowerEventType event_type;
  switch (power_supply.external_power()) {
    case power_manager::PowerSupplyProperties::AC:  // FALLTHROUGH
    case power_manager::PowerSupplyProperties::USB:
      event_type = PowerEventType::kAcInserted;
      break;
    case power_manager::PowerSupplyProperties::DISCONNECTED:
      event_type = PowerEventType::kAcRemoved;
      break;
    default:
      LOG(ERROR) << "Unknown external power type: "
                 << power_supply.external_power();
      return;
  }

  // Do not send an event if the previous AC event was the same.
  if (external_power_ac_event_.has_value() &&
      external_power_ac_event_.value() == event_type) {
    VLOG(2) << "Received the same AC event: " << static_cast<int>(event_type);
    return;
  }

  external_power_ac_event_ = event_type;
  observers_.ForAllPtrs(
      [event_type](
          chromeos::cros_healthd::mojom::CrosHealthdPowerObserver* observer) {
        switch (event_type) {
          case PowerEventType::kAcInserted:
            observer->OnAcInserted();
            break;
          case PowerEventType::kAcRemoved:
            observer->OnAcRemoved();
            break;
        }
      });

  StopObservingPowerdIfNecessary();
}

void PowerEventsImpl::OnSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) {
  OnAnySuspendImminentSignal();
}

void PowerEventsImpl::OnDarkSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) {
  OnAnySuspendImminentSignal();
}

void PowerEventsImpl::OnSuspendDoneSignal(
    const power_manager::SuspendDone& suspend_done) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdPowerObserver* observer) {
        observer->OnOsResume();
      });

  StopObservingPowerdIfNecessary();
}

void PowerEventsImpl::OnAnySuspendImminentSignal() {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdPowerObserver* observer) {
        observer->OnOsSuspend();
      });

  StopObservingPowerdIfNecessary();
}

void PowerEventsImpl::StopObservingPowerdIfNecessary() {
  if (!observers_.empty())
    return;

  context_->powerd_adapter()->RemovePowerObserver(this);
  is_observing_powerd_ = false;
}

}  // namespace diagnostics
