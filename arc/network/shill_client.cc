// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/shill_client.h"

#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

namespace {

std::set<std::string> GetDevices(const brillo::Any& property_value) {
  std::set<std::string> devices;
  for (const auto& path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    std::string device = path.value();
    // Strip "/device/" prefix.
    devices.emplace(device.substr(device.find_last_of('/') + 1));
  }
  return devices;
}

}  // namespace

namespace arc_networkd {

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus) : bus_(bus) {
  manager_proxy_.reset(new org::chromium::flimflam::ManagerProxy(bus_));
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&ShillClient::OnManagerPropertyChange,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillClient::OnManagerPropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
}

void ShillClient::ScanDevices(
    const base::Callback<void(const std::set<std::string>&)>& callback) {
  brillo::VariantDictionary props;
  if (!manager_proxy_->GetProperties(&props, nullptr)) {
    LOG(ERROR) << "Unable to get manager properties";
    return;
  }
  const auto it = props.find(shill::kDevicesProperty);
  if (it == props.end()) {
    LOG(WARNING) << "Manager properties is missing devices";
    return;
  }
  callback.Run(GetDevices(it->second));
}

std::string ShillClient::GetDefaultInterface() {
  brillo::VariantDictionary manager_props;

  if (!manager_proxy_->GetProperties(&manager_props, nullptr)) {
    LOG(ERROR) << "Unable to get manager properties";
    return "";
  }

  auto it = manager_props.find(shill::kDefaultServiceProperty);
  if (it == manager_props.end()) {
    LOG(WARNING) << "Manager properties is missing default service";
    return "";
  }

  dbus::ObjectPath service_path = it->second.TryGet<dbus::ObjectPath>();
  if (!service_path.IsValid() || service_path.value() == "/") {
    return "";
  }

  org::chromium::flimflam::ServiceProxy service_proxy(bus_, service_path);
  brillo::VariantDictionary service_props;
  if (!service_proxy.GetProperties(&service_props, nullptr)) {
    LOG(ERROR) << "Can't retrieve properties for service";
    return "";
  }

  it = service_props.find(shill::kIsConnectedProperty);
  if (it == service_props.end()) {
    LOG(WARNING) << "Service properties is missing \"IsConnected\"";
    return "";
  }
  if (!it->second.TryGet<bool>()) {
    LOG(INFO) << "Ignoring non-connected service";
    return "";
  }

  it = service_props.find(shill::kDeviceProperty);
  if (it == service_props.end()) {
    LOG(WARNING) << "Service properties is missing device path";
    return "";
  }

  dbus::ObjectPath device_path = it->second.TryGet<dbus::ObjectPath>();
  if (!device_path.IsValid()) {
    LOG(WARNING) << "Invalid device path";
    return "";
  }

  org::chromium::flimflam::DeviceProxy device_proxy(bus_, device_path);
  brillo::VariantDictionary device_props;
  if (!device_proxy.GetProperties(&device_props, nullptr)) {
    LOG(ERROR) << "Can't retrieve properties for device";
    return "";
  }

  it = device_props.find(shill::kInterfaceProperty);
  if (it == device_props.end()) {
    LOG(WARNING) << "Device properties is missing interface name";
    return "";
  }

  std::string interface = it->second.TryGet<std::string>();
  if (interface.empty()) {
    LOG(WARNING) << "Device interface name is empty";
  }

  return interface;
}

void ShillClient::OnManagerPropertyChangeRegistration(
    const std::string& interface,
    const std::string& signal_name,
    bool success) {
  if (!success)
    LOG(FATAL) << "Unable to register for interface change events";
}

void ShillClient::OnManagerPropertyChange(const std::string& property_name,
                                          const brillo::Any& property_value) {
  if (property_name == shill::kDevicesProperty) {
    devices_ = GetDevices(property_value);
    // TODO(b:132574450) Only trigger the callback if the content of
    // |devices_| has actually changed.
    if (!devices_callback_.is_null())
      devices_callback_.Run(devices_);

    // Choose a fallback interface when any network device exist. Update the
    // fallback interface if it that device does not exist anymore.
    if (!devices_.empty() &&
        devices_.find(fallback_default_interface_) == devices_.end()) {
      fallback_default_interface_ = *devices_.begin();
      // When the system appears to have no default interface, use the fallback
      // interface instead.
      if (default_interface_.empty() ||
          default_interface_ != fallback_default_interface_)
        SetDefaultInterface(fallback_default_interface_);
    }

    // Remove the fallback interface when no network device is managed by shill.
    if (!fallback_default_interface_.empty() && devices_.empty()) {
      fallback_default_interface_ = "";
      SetDefaultInterface("");
    }

    return;
  }

  if (property_name != shill::kDefaultServiceProperty &&
      property_name != shill::kConnectionStateProperty)
    return;

  SetDefaultInterface(GetDefaultInterface());
}

void ShillClient::SetDefaultInterface(std::string new_default) {
  // When the system default is lost, use the fallback interface instead.
  if (new_default.empty())
    new_default = fallback_default_interface_;

  if (default_interface_ == new_default)
    return;

  default_interface_ = new_default;
  for (const auto& cb : default_interface_callbacks_) {
    if (!cb.is_null())
      cb.Run(default_interface_);
  }
}

void ShillClient::RegisterDefaultInterfaceChangedHandler(
    const base::Callback<void(const std::string&)>& callback) {
  default_interface_callbacks_.emplace_back(callback);
  SetDefaultInterface(GetDefaultInterface());
  callback.Run(default_interface_);
}

void ShillClient::RegisterDevicesChangedHandler(
    const base::Callback<void(const std::set<std::string>&)>& callback) {
  devices_callback_ = callback;
}

void ShillClient::UnregisterDevicesChangedHandler() {
  devices_callback_.Reset();
}

}  // namespace arc_networkd
