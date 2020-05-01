// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/common/system/bluetooth_client_impl.h"

#include <base/bind.h>
#include <base/logging.h>
#include <dbus/bluetooth/dbus-constants.h>

namespace diagnostics {

namespace {

bool AreAdapterPropertiesValid(
    const BluetoothClient::AdapterProperties& adapter_properties) {
  return adapter_properties.name.is_valid() &&
         adapter_properties.address.is_valid() &&
         adapter_properties.powered.is_valid();
}

bool AreDevicePropertiesValid(
    const BluetoothClient::DeviceProperties& device_properties) {
  return device_properties.name.is_valid() &&
         device_properties.address.is_valid() &&
         device_properties.connected.is_valid();
}

}  // namespace

BluetoothClientImpl::BluetoothClientImpl(const scoped_refptr<dbus::Bus>& bus)
    : object_manager_(bus->GetObjectManager(
          bluez_object_manager::kBluezObjectManagerServiceName,
          dbus::ObjectPath(
              bluez_object_manager::kBluezObjectManagerServicePath))),
      weak_ptr_factory_(this) {
  DCHECK(object_manager_);
  object_manager_->RegisterInterface(
      bluetooth_adapter::kBluetoothAdapterInterface, this);
  object_manager_->RegisterInterface(
      bluetooth_device::kBluetoothDeviceInterface, this);
}

BluetoothClientImpl::~BluetoothClientImpl() {
  object_manager_->UnregisterInterface(
      bluetooth_adapter::kBluetoothAdapterInterface);
  object_manager_->UnregisterInterface(
      bluetooth_device::kBluetoothDeviceInterface);
}

dbus::PropertySet* BluetoothClientImpl::CreateProperties(
    dbus::ObjectProxy* object_proxy,
    const dbus::ObjectPath& object_path,
    const std::string& interface_name) {
  VLOG(3) << __func__ << " " << object_path.value() << " " << interface_name;

  auto callback =
      base::Bind(&BluetoothClientImpl::PropertyChanged,
                 weak_ptr_factory_.GetWeakPtr(), object_path, interface_name);
  if (interface_name == bluetooth_adapter::kBluetoothAdapterInterface) {
    return new AdapterProperties(object_proxy, callback);
  }
  if (interface_name == bluetooth_device::kBluetoothDeviceInterface) {
    return new DeviceProperties(object_proxy, callback);
  }
  NOTREACHED() << "Invalid interface name: " << interface_name;
  return nullptr;
}

void BluetoothClientImpl::ObjectAdded(const dbus::ObjectPath& object_path,
                                      const std::string& interface_name) {
  VLOG(3) << __func__ << " " << object_path.value() << " " << interface_name;

  dbus::PropertySet* properties =
      object_manager_->GetProperties(object_path, interface_name);
  if (!properties) {
    VLOG(3) << "Not found properties for " << object_path.value();
    return;
  }

  if (interface_name == bluetooth_adapter::kBluetoothAdapterInterface) {
    auto adapter_properties = static_cast<AdapterProperties*>(properties);
    if (!AreAdapterPropertiesValid(*adapter_properties)) {
      return;
    }
    for (auto& observer : observers_)
      observer.AdapterAdded(object_path, *adapter_properties);
    return;
  }
  if (interface_name == bluetooth_device::kBluetoothDeviceInterface) {
    auto device_properties = static_cast<DeviceProperties*>(properties);
    if (!AreDevicePropertiesValid(*device_properties)) {
      return;
    }
    for (auto& observer : observers_)
      observer.DeviceAdded(object_path, *device_properties);
    return;
  }
  NOTREACHED() << "Invalid interface name: " << interface_name;
}

void BluetoothClientImpl::ObjectRemoved(const dbus::ObjectPath& object_path,
                                        const std::string& interface_name) {
  VLOG(3) << __func__ << " " << object_path.value() << " " << interface_name;

  if (interface_name == bluetooth_adapter::kBluetoothAdapterInterface) {
    for (auto& observer : observers_)
      observer.AdapterRemoved(object_path);
    return;
  }
  if (interface_name == bluetooth_device::kBluetoothDeviceInterface) {
    for (auto& observer : observers_)
      observer.DeviceRemoved(object_path);
    return;
  }
  NOTREACHED() << "Invalid interface name: " << interface_name;
}

void BluetoothClientImpl::PropertyChanged(const dbus::ObjectPath& object_path,
                                          const std::string& interface_name,
                                          const std::string& property_name) {
  VLOG(3) << __func__ << " " << object_path.value() << " " << interface_name
          << " " << property_name;

  dbus::PropertySet* properties =
      object_manager_->GetProperties(object_path, interface_name);
  if (!properties) {
    VLOG(3) << "Not found properties for " << object_path.value();
    return;
  }

  if (interface_name == bluetooth_adapter::kBluetoothAdapterInterface) {
    auto adapter_properties = static_cast<AdapterProperties*>(properties);
    if (!AreAdapterPropertiesValid(*adapter_properties)) {
      return;
    }
    for (auto& observer : observers_)
      observer.AdapterPropertyChanged(object_path, *adapter_properties);
    return;
  }
  if (interface_name == bluetooth_device::kBluetoothDeviceInterface) {
    auto device_properties = static_cast<DeviceProperties*>(properties);
    if (!AreDevicePropertiesValid(*device_properties)) {
      return;
    }
    for (auto& observer : observers_)
      observer.DevicePropertyChanged(object_path, *device_properties);
    return;
  }
  NOTREACHED() << "Invalid interface name: " << interface_name;
}

}  // namespace diagnostics
