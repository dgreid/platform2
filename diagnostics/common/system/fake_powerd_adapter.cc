// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/fake_powerd_adapter.h"

namespace diagnostics {

FakePowerdAdapter::FakePowerdAdapter() = default;
FakePowerdAdapter::~FakePowerdAdapter() = default;

// PowerdAdapter overrides:
void FakePowerdAdapter::AddPowerObserver(PowerObserver* observer) {
  power_observers_.AddObserver(observer);
}

void FakePowerdAdapter::RemovePowerObserver(PowerObserver* observer) {
  power_observers_.RemoveObserver(observer);
}

void FakePowerdAdapter::AddLidObserver(LidObserver* observer) {
  lid_observers_.AddObserver(observer);
}

void FakePowerdAdapter::RemoveLidObserver(LidObserver* observer) {
  lid_observers_.RemoveObserver(observer);
}

base::Optional<power_manager::PowerSupplyProperties>
FakePowerdAdapter::GetPowerSupplyProperties() {
  return power_supply_properties_;
}

bool FakePowerdAdapter::HasPowerObserver(PowerObserver* observer) const {
  return power_observers_.HasObserver(observer);
}

bool FakePowerdAdapter::HasLidObserver(LidObserver* observer) const {
  return lid_observers_.HasObserver(observer);
}

void FakePowerdAdapter::EmitPowerSupplyPollSignal(
    const power_manager::PowerSupplyProperties& power_supply) const {
  for (auto& observer : power_observers_) {
    observer.OnPowerSupplyPollSignal(power_supply);
  }
}

void FakePowerdAdapter::EmitSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) const {
  for (auto& observer : power_observers_) {
    observer.OnSuspendImminentSignal(suspend_imminent);
  }
}

void FakePowerdAdapter::EmitDarkSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) const {
  for (auto& observer : power_observers_) {
    observer.OnDarkSuspendImminentSignal(suspend_imminent);
  }
}

void FakePowerdAdapter::EmitSuspendDoneSignal(
    const power_manager::SuspendDone& suspend_done) const {
  for (auto& observer : power_observers_) {
    observer.OnSuspendDoneSignal(suspend_done);
  }
}

void FakePowerdAdapter::EmitLidClosedSignal() const {
  for (auto& observer : lid_observers_) {
    observer.OnLidClosedSignal();
  }
}

void FakePowerdAdapter::EmitLidOpenedSignal() const {
  for (auto& observer : lid_observers_) {
    observer.OnLidOpenedSignal();
  }
}

void FakePowerdAdapter::SetPowerSupplyProperties(
    base::Optional<power_manager::PowerSupplyProperties> properties) {
  power_supply_properties_ = properties;
}

}  // namespace diagnostics
