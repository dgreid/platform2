// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_
#define DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_

#include <base/observer_list_types.h>
#include <base/optional.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

namespace diagnostics {

constexpr char kPowerdPowerSupplyPropertiesFailedMessage[] =
    "Failed to get power supply properties from powerd.";

// Adapter for communication with powerd daemon.
class PowerdAdapter {
 public:
  // Observes general power events.
  class PowerObserver : public base::CheckedObserver {
   public:
    virtual ~PowerObserver() = default;

    virtual void OnPowerSupplyPollSignal(
        const power_manager::PowerSupplyProperties& power_supply) = 0;
    virtual void OnSuspendImminentSignal(
        const power_manager::SuspendImminent& suspend_imminent) = 0;
    virtual void OnDarkSuspendImminentSignal(
        const power_manager::SuspendImminent& suspend_imminent) = 0;
    virtual void OnSuspendDoneSignal(
        const power_manager::SuspendDone& suspend_done) = 0;
  };

  // Observes lid events.
  class LidObserver : public base::CheckedObserver {
   public:
    virtual ~LidObserver() = default;

    virtual void OnLidClosedSignal() = 0;
    virtual void OnLidOpenedSignal() = 0;
  };

  virtual ~PowerdAdapter() = default;

  virtual void AddPowerObserver(PowerObserver* observer) = 0;
  virtual void RemovePowerObserver(PowerObserver* observer) = 0;

  virtual void AddLidObserver(LidObserver* observer) = 0;
  virtual void RemoveLidObserver(LidObserver* observer) = 0;

  // Returns a PowerSupplyProperties proto from powerd on success. Will return a
  // base::nullopt if the powerd service is not available or the D-Bus response
  // cannot be parsed into the proto structure.
  virtual base::Optional<power_manager::PowerSupplyProperties>
  GetPowerSupplyProperties() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_H_
