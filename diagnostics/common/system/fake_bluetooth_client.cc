// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/fake_bluetooth_client.h"

#include <base/logging.h>

namespace diagnostics {

FakeBluetoothClient::FakeBluetoothClient() = default;
FakeBluetoothClient::~FakeBluetoothClient() = default;

bool FakeBluetoothClient::HasObserver(Observer* observer) const {
  return observers_.HasObserver(observer);
}

void FakeBluetoothClient::EmitAdapterAdded(
    const dbus::ObjectPath& object_path,
    const AdapterProperties& properties) const {
  for (auto& observer : observers_) {
    observer.AdapterAdded(object_path, properties);
  }
}

void FakeBluetoothClient::EmitAdapterRemoved(
    const dbus::ObjectPath& object_path) const {
  for (auto& observer : observers_) {
    observer.AdapterRemoved(object_path);
  }
}

void FakeBluetoothClient::EmitAdapterPropertyChanged(
    const dbus::ObjectPath& object_path,
    const AdapterProperties& properties) const {
  for (auto& observer : observers_) {
    observer.AdapterPropertyChanged(object_path, properties);
  }
}

void FakeBluetoothClient::EmitDeviceAdded(
    const dbus::ObjectPath& object_path,
    const DeviceProperties& properties) const {
  for (auto& observer : observers_) {
    observer.DeviceAdded(object_path, properties);
  }
}

void FakeBluetoothClient::EmitDeviceRemoved(
    const dbus::ObjectPath& object_path) const {
  for (auto& observer : observers_) {
    observer.DeviceRemoved(object_path);
  }
}

void FakeBluetoothClient::EmitDevicePropertyChanged(
    const dbus::ObjectPath& object_path,
    const DeviceProperties& properties) const {
  for (auto& observer : observers_) {
    observer.DevicePropertyChanged(object_path, properties);
  }
}

}  // namespace diagnostics
