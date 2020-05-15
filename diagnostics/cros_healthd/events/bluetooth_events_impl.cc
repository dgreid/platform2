// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/events/bluetooth_events_impl.h"

#include <utility>

namespace diagnostics {

BluetoothEventsImpl::BluetoothEventsImpl(Context* context) : context_(context) {
  DCHECK(context_);
}

BluetoothEventsImpl::~BluetoothEventsImpl() {
  if (is_observing_bluetooth_client_)
    context_->bluetooth_client()->RemoveObserver(this);
}

void BluetoothEventsImpl::AddObserver(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer) {
  if (!is_observing_bluetooth_client_) {
    context_->bluetooth_client()->AddObserver(this);
    is_observing_bluetooth_client_ = true;
  }

  observers_.AddPtr(std::move(observer));
}

void BluetoothEventsImpl::AdapterAdded(
    const dbus::ObjectPath& adapter_path,
    const BluetoothClient::AdapterProperties& properties) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnAdapterAdded(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::AdapterRemoved(const dbus::ObjectPath& adapter_path) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnAdapterRemoved(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::AdapterPropertyChanged(
    const dbus::ObjectPath& adapter_path,
    const BluetoothClient::AdapterProperties& properties) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnAdapterPropertyChanged(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::DeviceAdded(
    const dbus::ObjectPath& device_path,
    const BluetoothClient::DeviceProperties& properties) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnDeviceAdded(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::DeviceRemoved(const dbus::ObjectPath& device_path) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnDeviceRemoved(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::DevicePropertyChanged(
    const dbus::ObjectPath& device_path,
    const BluetoothClient::DeviceProperties& properties) {
  observers_.ForAllPtrs(
      [](chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserver*
             observer) { observer->OnDevicePropertyChanged(); });
  StopObservingBluetoothClientIfNecessary();
}

void BluetoothEventsImpl::StopObservingBluetoothClientIfNecessary() {
  if (!observers_.empty())
    return;

  context_->bluetooth_client()->RemoveObserver(this);
  is_observing_bluetooth_client_ = false;
}

}  // namespace diagnostics
