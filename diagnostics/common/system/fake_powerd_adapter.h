// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_FAKE_POWERD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_FAKE_POWERD_ADAPTER_H_

#include <base/macros.h>
#include <base/observer_list.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

#include "diagnostics/common/system/powerd_adapter.h"

namespace diagnostics {

class FakePowerdAdapter : public PowerdAdapter {
 public:
  FakePowerdAdapter();
  ~FakePowerdAdapter() override;

  // PowerdAdapter overrides:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

  bool HasObserver(Observer* observer) const;

  void EmitPowerSupplyPollSignal(
      const power_manager::PowerSupplyProperties& power_supply) const;
  void EmitSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) const;
  void EmitDarkSuspendImminentSignal(
      const power_manager::SuspendImminent& suspend_imminent) const;
  void EmitSuspendDoneSignal(
      const power_manager::SuspendDone& suspend_done) const;

 private:
  base::ObserverList<Observer> observers_;

  DISALLOW_COPY_AND_ASSIGN(FakePowerdAdapter);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_FAKE_POWERD_ADAPTER_H_
