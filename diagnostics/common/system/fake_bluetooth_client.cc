// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/fake_bluetooth_client.h"

#include <base/logging.h>

namespace diagnostics {

FakeBluetoothClient::FakeBluetoothClient() = default;
FakeBluetoothClient::~FakeBluetoothClient() = default;

std::vector<dbus::ObjectPath> FakeBluetoothClient::GetAdapters() {
  NOTIMPLEMENTED() << "Use MockBluetoothClient to get adapters";
  return {};
}

std::vector<dbus::ObjectPath> FakeBluetoothClient::GetDevices() {
  NOTIMPLEMENTED() << "Use MockBluetoothClient to get devices";
  return {};
}

const BluetoothClient::AdapterProperties*
FakeBluetoothClient::GetAdapterProperties(
    const dbus::ObjectPath& adapter_path) {
  NOTIMPLEMENTED() << "Use MockBluetoothClient to get adapter properties";
  return nullptr;
}

const BluetoothClient::DeviceProperties*
FakeBluetoothClient::GetDeviceProperties(const dbus::ObjectPath& device_path) {
  NOTIMPLEMENTED() << "Use MockBluetoothClient to get device properties";
  return nullptr;
}

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
