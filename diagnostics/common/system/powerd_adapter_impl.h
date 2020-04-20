// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
#define DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_

#include <memory>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <base/observer_list.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <power_manager/proto_bindings/power_supply_properties.pb.h>
#include <power_manager/proto_bindings/suspend.pb.h>

#include "diagnostics/common/system/powerd_adapter.h"

namespace diagnostics {

// PowerdAdapter interface implementation that observes D-Bus signals from
// powerd daemon.
class PowerdAdapterImpl : public PowerdAdapter {
 public:
  explicit PowerdAdapterImpl(const scoped_refptr<dbus::Bus>& bus);

  ~PowerdAdapterImpl() override;

  // PowerdAdapter overrides:
  void AddObserver(Observer* observer) override;
  void RemoveObserver(Observer* observer) override;

 private:
  // Handles PowerSupplyPoll signals emitted by powerd daemon.
  void HandlePowerSupplyPoll(dbus::Signal* signal);

  // Handles SuspendImminent signals emitted by powerd daemon.
  void HandleSuspendImminent(dbus::Signal* signal);

  // Handles DarkSuspendImminent signals emitted by powerd daemon.
  void HandleDarkSuspendImminent(dbus::Signal* signal);

  // Handles SuspendDone signals emitted by powerd daemon.
  void HandleSuspendDone(dbus::Signal* signal);

  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<PowerdAdapterImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(PowerdAdapterImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_POWERD_ADAPTER_IMPL_H_
