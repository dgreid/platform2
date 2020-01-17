// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/shill_client.h"

#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

namespace arc_networkd {

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus) : bus_(bus) {
  manager_proxy_.reset(new org::chromium::flimflam::ManagerProxy(bus_));
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&ShillClient::OnManagerPropertyChange,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillClient::OnManagerPropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
}

const std::string& ShillClient::default_interface() const {
  return default_interface_;
}

const std::set<std::string> ShillClient::get_devices() const {
  return devices_;
}

bool ShillClient::has_device(const std::string& ifname) const {
  return devices_.find(ifname) != devices_.end();
}

void ShillClient::ScanDevices(const DevicesChangeHandler& handler) {
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
  UpdateDevices(it->second);
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
    UpdateDevices(property_value);

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

std::string ShillClient::SetDefaultInterface(std::string new_default) {
  // When the system default is lost, use the fallback interface instead.
  if (new_default.empty())
    new_default = fallback_default_interface_;

  if (default_interface_ == new_default)
    return default_interface_;

  LOG(INFO) << "Default interface changed from [" << default_interface_
            << "] to [" << new_default << "]";

  const std::string prev_default = default_interface_;
  default_interface_ = new_default;
  for (const auto& h : default_interface_handlers_) {
    if (!h.is_null())
      h.Run(default_interface_, prev_default);
  }
  return prev_default;
}

void ShillClient::RegisterDefaultInterfaceChangedHandler(
    const DefaultInterfaceChangeHandler& handler) {
  default_interface_handlers_.emplace_back(handler);
  const auto prev_default = SetDefaultInterface(GetDefaultInterface());
  handler.Run(default_interface_, prev_default);
}

void ShillClient::RegisterDevicesChangedHandler(
    const DevicesChangeHandler& handler) {
  device_handlers_.emplace_back(handler);
}

void ShillClient::UpdateDevices(const brillo::Any& property_value) {
  std::set<std::string> new_devices, added, removed;
  for (const auto& path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    std::string device = path.value();
    // Strip "/device/" prefix.
    device = device.substr(device.find_last_of('/') + 1);

    new_devices.emplace(device);
    if (devices_.find(device) == devices_.end())
      added.insert(device);
  }

  for (const auto& d : devices_) {
    if (new_devices.find(d) == new_devices.end())
      removed.insert(d);
  }

  devices_ = new_devices;

  for (const auto& h : device_handlers_)
    h.Run(added, removed);
}

}  // namespace arc_networkd
