// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_

#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

namespace diagnostics {

// Adapter for communication with powerd daemon.
class PowerdAdapter {
 public:
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void OnPowerSupplyPollSignal(
        const power_manager::PowerSupplyProperties& power_supply) = 0;
    virtual void OnSuspendImminentSignal(
        const power_manager::SuspendImminent& suspend_imminent) = 0;
    virtual void OnDarkSuspendImminentSignal(
        const power_manager::SuspendImminent& suspend_imminent) = 0;
    virtual void OnSuspendDoneSignal(
        const power_manager::SuspendDone& suspend_done) = 0;
  };

  virtual ~PowerdAdapter() = default;

  virtual void AddObserver(Observer* observer) = 0;
  virtual void RemoveObserver(Observer* observer) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_
