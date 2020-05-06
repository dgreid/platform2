// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/powerd_adapter_impl.h"

#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <dbus/object_proxy.h>
#include <dbus/power_manager/dbus-constants.h>

namespace diagnostics {

namespace {

// Handles the result of an attempt to connect to a D-Bus signal.
void HandleSignalConnected(const std::string& interface,
                           const std::string& signal,
                           bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << interface << "." << signal;
    return;
  }
  VLOG(2) << "Successfully connected to D-Bus signal " << interface << "."
          << signal;
}

}  // namespace

PowerdAdapterImpl::PowerdAdapterImpl(const scoped_refptr<dbus::Bus>& bus)
    : weak_ptr_factory_(this) {
  DCHECK(bus);

  dbus::ObjectProxy* bus_proxy = bus->GetObjectProxy(
      power_manager::kPowerManagerServiceName,
      dbus::ObjectPath(power_manager::kPowerManagerServicePath));
  DCHECK(bus_proxy);

  bus_proxy->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kPowerSupplyPollSignal,
      base::Bind(&PowerdAdapterImpl::HandlePowerSupplyPoll,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&HandleSignalConnected));
  bus_proxy->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kSuspendImminentSignal,
      base::Bind(&PowerdAdapterImpl::HandleSuspendImminent,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&HandleSignalConnected));
  bus_proxy->ConnectToSignal(
      power_manager::kPowerManagerInterface,
      power_manager::kDarkSuspendImminentSignal,
      base::Bind(&PowerdAdapterImpl::HandleDarkSuspendImminent,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&HandleSignalConnected));
  bus_proxy->ConnectToSignal(power_manager::kPowerManagerInterface,
                             power_manager::kSuspendDoneSignal,
                             base::Bind(&PowerdAdapterImpl::HandleSuspendDone,
                                        weak_ptr_factory_.GetWeakPtr()),
                             base::Bind(&HandleSignalConnected));
  bus_proxy->ConnectToSignal(power_manager::kPowerManagerInterface,
                             power_manager::kLidClosedSignal,
                             base::Bind(&PowerdAdapterImpl::HandleLidClosed,
                                        weak_ptr_factory_.GetWeakPtr()),
                             base::Bind(&HandleSignalConnected));
  bus_proxy->ConnectToSignal(power_manager::kPowerManagerInterface,
                             power_manager::kLidOpenedSignal,
                             base::Bind(&PowerdAdapterImpl::HandleLidOpened,
                                        weak_ptr_factory_.GetWeakPtr()),
                             base::Bind(&HandleSignalConnected));
}

PowerdAdapterImpl::~PowerdAdapterImpl() = default;

void PowerdAdapterImpl::AddPowerObserver(PowerObserver* observer) {
  DCHECK(observer);
  power_observers_.AddObserver(observer);
}

void PowerdAdapterImpl::RemovePowerObserver(PowerObserver* observer) {
  DCHECK(observer);
  power_observers_.RemoveObserver(observer);
}

void PowerdAdapterImpl::AddLidObserver(LidObserver* observer) {
  DCHECK(observer);
  lid_observers_.AddObserver(observer);
}

void PowerdAdapterImpl::RemoveLidObserver(LidObserver* observer) {
  DCHECK(observer);
  lid_observers_.RemoveObserver(observer);
}

void PowerdAdapterImpl::HandlePowerSupplyPoll(dbus::Signal* signal) {
  DCHECK(signal);

  dbus::MessageReader reader(signal);
  power_manager::PowerSupplyProperties proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Unable to parse PowerSupplyPoll signal";
    return;
  }

  for (auto& observer : power_observers_)
    observer.OnPowerSupplyPollSignal(proto);
}

void PowerdAdapterImpl::HandleSuspendImminent(dbus::Signal* signal) {
  DCHECK(signal);

  dbus::MessageReader reader(signal);
  power_manager::SuspendImminent proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Unable to parse SuspendImminent signal";
    return;
  }

  for (auto& observer : power_observers_)
    observer.OnSuspendImminentSignal(proto);
}

void PowerdAdapterImpl::HandleDarkSuspendImminent(dbus::Signal* signal) {
  DCHECK(signal);

  dbus::MessageReader reader(signal);
  power_manager::SuspendImminent proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Unable to parse DarkSuspendImminent signal";
    return;
  }

  for (auto& observer : power_observers_)
    observer.OnDarkSuspendImminentSignal(proto);
}

void PowerdAdapterImpl::HandleSuspendDone(dbus::Signal* signal) {
  DCHECK(signal);

  dbus::MessageReader reader(signal);
  power_manager::SuspendDone proto;
  if (!reader.PopArrayOfBytesAsProto(&proto)) {
    LOG(ERROR) << "Unable to parse SuspendDone signal";
    return;
  }

  for (auto& observer : power_observers_)
    observer.OnSuspendDoneSignal(proto);
}

void PowerdAdapterImpl::HandleLidClosed(dbus::Signal* signal) {
  for (auto& observer : lid_observers_)
    observer.OnLidClosedSignal();
}

void PowerdAdapterImpl::HandleLidOpened(dbus::Signal* signal) {
  for (auto& observer : lid_observers_)
    observer.OnLidOpenedSignal();
}

}  // namespace diagnostics
