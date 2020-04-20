// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/fake_powerd_adapter.h"

namespace diagnostics {

FakePowerdAdapter::FakePowerdAdapter() = default;
FakePowerdAdapter::~FakePowerdAdapter() = default;

// PowerdAdapter overrides:
void FakePowerdAdapter::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}
void FakePowerdAdapter::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool FakePowerdAdapter::HasObserver(Observer* observer) const {
  return observers_.HasObserver(observer);
}

void FakePowerdAdapter::EmitPowerSupplyPollSignal(
    const power_manager::PowerSupplyProperties& power_supply) const {
  for (auto& observer : observers_) {
    observer.OnPowerSupplyPollSignal(power_supply);
  }
}

void FakePowerdAdapter::EmitSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) const {
  for (auto& observer : observers_) {
    observer.OnSuspendImminentSignal(suspend_imminent);
  }
}

void FakePowerdAdapter::EmitDarkSuspendImminentSignal(
    const power_manager::SuspendImminent& suspend_imminent) const {
  for (auto& observer : observers_) {
    observer.OnDarkSuspendImminentSignal(suspend_imminent);
  }
}

void FakePowerdAdapter::EmitSuspendDoneSignal(
    const power_manager::SuspendDone& suspend_done) const {
  for (auto& observer : observers_) {
    observer.OnSuspendDoneSignal(suspend_done);
  }
}

}  // namespace diagnostics
