// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_

#include <dbus/object_path.h>
#include <mojo/public/cpp/bindings/interface_ptr_set.h>

#include "diagnostics/cros_healthd/events/bluetooth_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_events.mojom.h"

namespace diagnostics {

// Production implementation of the BluetoothEvents interface.
class BluetoothEventsImpl final : public BluetoothEvents,
                                  public BluetoothClient::Observer {
 public:
  explicit BluetoothEventsImpl(Context* context);
  BluetoothEventsImpl(const BluetoothEventsImpl&) = delete;
  BluetoothEventsImpl& operator=(const BluetoothEventsImpl&) = delete;
  ~BluetoothEventsImpl() override;

  // BluetoothEvents overrides:
  void AddObserver(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer)
      override;

 private:
  // BluetoothClient::Observer overrides:
  void AdapterAdded(
      const dbus::ObjectPath& adapter_path,
      const BluetoothClient::AdapterProperties& properties) override;
  void AdapterRemoved(const dbus::ObjectPath& adapter_path) override;
  void AdapterPropertyChanged(
      const dbus::ObjectPath& adapter_path,
      const BluetoothClient::AdapterProperties& properties) override;
  void DeviceAdded(
      const dbus::ObjectPath& device_path,
      const BluetoothClient::DeviceProperties& properties) override;
  void DeviceRemoved(const dbus::ObjectPath& device_path) override;
  void DevicePropertyChanged(
      const dbus::ObjectPath& device_path,
      const BluetoothClient::DeviceProperties& properties) override;

  // Checks to see if any observers are left. If not, removes this object from
  // the BluetoothClient's observers.
  void StopObservingBluetoothClientIfNecessary();

  // Tracks whether or not this instance has added itself as an observer of
  // the BluetoothClient.
  bool is_observing_bluetooth_client_ = false;

  // Each observer in |observers_| will be notified of any Bluetooth event in
  // the chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver interface.
  // The InterfacePtrSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::InterfacePtrSet<
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver>
      observers_;

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_BLUETOOTH_EVENTS_IMPL_H_
