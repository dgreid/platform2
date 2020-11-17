// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/client/client.h"

#include <set>

#include <base/bind.h>
#include <base/logging.h>
#include <brillo/variant_dictionary.h>

using org::chromium::flimflam::DeviceProxy;
using org::chromium::flimflam::DeviceProxyInterface;
using org::chromium::flimflam::IPConfigProxy;
using org::chromium::flimflam::IPConfigProxyInterface;
using org::chromium::flimflam::ManagerProxy;
using org::chromium::flimflam::ManagerProxyInterface;
using org::chromium::flimflam::ServiceProxy;
using org::chromium::flimflam::ServiceProxyInterface;

namespace shill {
namespace {

Client::Device::Type ParseDeviceType(const std::string& type_str) {
  static const std::map<std::string, Client::Device::Type> str2enum{
      {shill::kTypeCellular, Client::Device::Type::kCellular},
      {shill::kTypeEthernet, Client::Device::Type::kEthernet},
      {shill::kTypeEthernetEap, Client::Device::Type::kEthernetEap},
      {shill::kTypeGuestInterface, Client::Device::Type::kGuestInterface},
      {shill::kTypeLoopback, Client::Device::Type::kLoopback},
      {shill::kTypePPP, Client::Device::Type::kPPP},
      {shill::kTypePPPoE, Client::Device::Type::kPPPoE},
      {shill::kTypeTunnel, Client::Device::Type::kTunnel},
      {shill::kTypeWifi, Client::Device::Type::kWifi},
      {shill::kTypeVPN, Client::Device::Type::kVPN},
  };

  const auto it = str2enum.find(type_str);
  return it != str2enum.end() ? it->second : Client::Device::Type::kUnknown;
}

}  // namespace

Client::Client(scoped_refptr<dbus::Bus> bus) : bus_(bus) {}

void Client::Init() {
  bus_->GetObjectProxy(kFlimflamServiceName, dbus::ObjectPath{"/"})
      ->SetNameOwnerChangedCallback(
          base::Bind(&Client::OnOwnerChange, weak_factory_.GetWeakPtr()));
  SetupManagerProxy();
}

void Client::NewManagerProxy() {
  manager_proxy_ = std::make_unique<ManagerProxy>(bus_);
}

void Client::SetupManagerProxy() {
  NewManagerProxy();
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&Client::OnManagerPropertyChange, weak_factory_.GetWeakPtr()),
      base::Bind(&Client::OnManagerPropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
}

void Client::ReleaseManagerProxy() {
  devices_.clear();
  bus_->RemoveObjectProxy(kFlimflamServiceName, dbus::ObjectPath("/"),
                          base::DoNothing());
  manager_proxy_.reset();
}

void Client::NewDefaultServiceProxy(const dbus::ObjectPath& service_path) {
  default_service_proxy_ = std::make_unique<ServiceProxy>(bus_, service_path);
}

void Client::SetupDefaultServiceProxy(const dbus::ObjectPath& service_path) {
  if (!service_path.IsValid() || service_path.value() == "/")
    return;

  NewDefaultServiceProxy(service_path);
  default_service_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&Client::OnDefaultServicePropertyChange,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&Client::OnDefaultServicePropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
}

void Client::ReleaseDefaultServiceProxy() {
  default_service_connected_ = false;
  default_device_path_.clear();

  if (default_service_proxy_) {
    bus_->RemoveObjectProxy(kFlimflamServiceName,
                            default_service_proxy_->GetObjectPath(),
                            base::DoNothing());
    default_service_proxy_.reset();
  }
}

std::unique_ptr<DeviceProxyInterface> Client::NewDeviceProxy(
    const dbus::ObjectPath& device_path) {
  return std::make_unique<DeviceProxy>(bus_, device_path);
}

void Client::SetupDeviceProxy(const dbus::ObjectPath& device_path) {
  auto proxy = NewDeviceProxy(device_path);
  auto* ptr = proxy.get();
  devices_.emplace(device_path.value(),
                   std::make_unique<DeviceWrapper>(bus_, std::move(proxy)));
  ptr->RegisterPropertyChangedSignalHandler(
      base::Bind(&Client::OnDevicePropertyChange, weak_factory_.GetWeakPtr(),
                 false /*device_added*/, device_path.value()),
      base::Bind(&Client::OnDevicePropertyChangeRegistration,
                 weak_factory_.GetWeakPtr(), device_path.value()));
}

void Client::RegisterDefaultServiceChangedHandler(
    const DefaultServiceChangedHandler& handler) {
  default_service_handlers_.emplace_back(handler);
}

void Client::RegisterDefaultDeviceChangedHandler(
    const DeviceChangedHandler& handler) {
  // Provide the current default device to the new handler.
  Device* device = nullptr;
  const auto it = devices_.find(default_device_path_);
  if (it != devices_.end())
    device = it->second->device();

  handler.Run(device);

  default_device_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceChangedHandler(const DeviceChangedHandler& handler) {
  device_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceAddedHandler(const DeviceChangedHandler& handler) {
  // Provide the current list of devices.
  for (const auto& kv : devices_) {
    handler.Run(kv.second->device());
  }

  device_added_handlers_.emplace_back(handler);
}

void Client::RegisterDeviceRemovedHandler(const DeviceChangedHandler& handler) {
  device_removed_handlers_.emplace_back(handler);
}

void Client::OnOwnerChange(const std::string& old_owner,
                           const std::string& new_owner) {
  ReleaseDefaultServiceProxy();
  ReleaseManagerProxy();

  if (new_owner.empty()) {
    LOG(INFO) << "Shill lost";
    return;
  }

  LOG(INFO) << "Shill reset";
  SetupManagerProxy();
}

void Client::OnManagerPropertyChangeRegistration(const std::string& interface,
                                                 const std::string& signal_name,
                                                 bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for Manager change events "
               << " for " << signal_name << " on " << interface;
    return;
  }
  brillo::VariantDictionary properties;
  if (!manager_proxy_ || !manager_proxy_->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get shill Manager properties";
    return;
  }

  for (const auto& prop : {kDevicesProperty, kDefaultServiceProperty}) {
    auto it = properties.find(prop);
    if (it != properties.end()) {
      OnManagerPropertyChange(prop, it->second);
    } else {
      LOG(ERROR) << "Cannot find Manager property [" << prop << "]";
    }
  }
}

void Client::OnManagerPropertyChange(const std::string& property_name,
                                     const brillo::Any& property_value) {
  if (property_name == kDefaultServiceProperty) {
    HandleDefaultServiceChanged(property_value);
    return;
  }

  if (property_name == kDevicesProperty) {
    HandleDevicesChanged(property_value);
    return;
  }
}

void Client::HandleDefaultServiceChanged(const brillo::Any& property_value) {
  dbus::ObjectPath cur_path,
      service_path = property_value.TryGet<dbus::ObjectPath>();
  if (default_service_proxy_)
    cur_path = default_service_proxy_->GetObjectPath();

  if (service_path != cur_path) {
    LOG(INFO) << "Default service changed from [" << cur_path.value()
              << "] to [" << service_path.value() << "]";
  }
  ReleaseDefaultServiceProxy();
  SetupDefaultServiceProxy(service_path);

  // Notify that the default service has changed.
  for (auto& handler : default_service_handlers_) {
    handler.Run();
  }
}

void Client::AddDevice(const dbus::ObjectPath& device_path) {
  const std::string& path = device_path.value();
  if (devices_.find(path) != devices_.end())
    return;

  LOG(INFO) << "Device [" << path << "] added";
  SetupDeviceProxy(device_path);
}

void Client::HandleDevicesChanged(const brillo::Any& property_value) {
  std::set<std::string> latest;
  for (const auto& path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    latest.emplace(path.value());
    AddDevice(path);
  }

  for (auto it = devices_.begin(); it != devices_.end();) {
    if (latest.find(it->first) == latest.end()) {
      LOG(INFO) << "Device [" << it->first << "] removed";
      for (auto& handler : device_removed_handlers_) {
        handler.Run(it->second->device());
      }
      it = devices_.erase(it);
    } else {
      ++it;
    }
  }
}

void Client::OnDefaultServicePropertyChangeRegistration(
    const std::string& interface,
    const std::string& signal_name,
    bool success) {
  if (!success) {
    std::string path;
    if (default_service_proxy_)
      path = default_service_proxy_->GetObjectPath().value();

    LOG(ERROR) << "Unable to register for Service [" << path
               << "] change events "
               << " for " << signal_name << " on " << interface;
    return;
  }

  if (!default_service_proxy_) {
    LOG(ERROR) << "No default service";
    return;
  }
  const std::string service_path =
      default_service_proxy_->GetObjectPath().value();
  brillo::VariantDictionary properties;
  if (!default_service_proxy_->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties for the default service ["
               << service_path << "]";
    return;
  }
  OnDefaultServicePropertyChange(
      kIsConnectedProperty,
      brillo::GetVariantValueOrDefault<bool>(properties, kIsConnectedProperty));
  OnDefaultServicePropertyChange(
      kDeviceProperty, brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
                           properties, kDeviceProperty));
}

void Client::OnDefaultServicePropertyChange(const std::string& property_name,
                                            const brillo::Any& property_value) {
  if (property_name == kIsConnectedProperty) {
    bool connected = property_value.TryGet<bool>();
    if (connected == default_service_connected_)
      return;

    std::string service_path;
    if (default_service_proxy_)
      service_path = default_service_proxy_->GetObjectPath().value();

    LOG(INFO) << "Default service [" << service_path << "] "
              << (connected ? "is now connected" : "disconnected");
    default_service_connected_ = connected;
  } else if (property_name == kDeviceProperty) {
    std::string path = property_value.TryGet<dbus::ObjectPath>().value();
    if (path == default_device_path_)
      return;

    LOG(INFO) << "Default service device changed from [" << default_device_path_
              << "] to [" << path << "]";
    default_device_path_ = path;
  } else {
    return;
  }

  // When there is no service, run the handlers with a nullptr to indicate this
  // condition.
  if (!default_service_connected_ || default_device_path_ == "" ||
      default_device_path_ == "/") {
    for (auto& handler : default_device_handlers_) {
      handler.Run(nullptr);
    }
    return;
  }

  // We generally expect to already be aware of the default device unless it
  // happens to be a VPN. In the case of the latter, add and track it (this will
  // ultimately fire the same handler after reading all the properties.
  const auto& it = devices_.find(default_device_path_);
  if (it != devices_.end()) {
    for (auto& handler : default_device_handlers_) {
      handler.Run(it->second->device());
    }
  } else {
    AddDevice(dbus::ObjectPath(default_device_path_));
  }
}

void Client::OnDevicePropertyChangeRegistration(const std::string& device_path,
                                                const std::string& interface,
                                                const std::string& signal_name,
                                                bool success) {
  if (!success) {
    LOG(ERROR) << "Unable to register for Device [" << device_path
               << "] change events "
               << " for " << signal_name << " on " << interface;
    return;
  }

  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Device [" << device_path << "] not found";
    return;
  }

  brillo::VariantDictionary properties;
  if (!it->second->proxy()->GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties for device [" << device_path << "]";
    return;
  }

  auto* device = it->second->device();
  device->type = ParseDeviceType(
      brillo::GetVariantValueOrDefault<std::string>(properties, kTypeProperty));
  if (device->type == Device::Type::kUnknown)
    LOG(ERROR) << "Device [" << device_path << "] type is unknown";

  device->ifname = brillo::GetVariantValueOrDefault<std::string>(
      properties, kInterfaceProperty);
  if (device->ifname.empty()) {
    LOG(ERROR) << "Device [" << device_path << "] has no interface";
    return;
  }
  // Set |device_added| to true here so it invokes the corresponding handler, if
  // applicable - this will occur only once (per device).
  OnDevicePropertyChange(
      true /*device_added*/, device_path, kIPConfigsProperty,
      brillo::GetVariantValueOrDefault<std::vector<dbus::ObjectPath>>(
          properties, kIPConfigsProperty));
}

void Client::OnDevicePropertyChange(bool device_added,
                                    const std::string& device_path,
                                    const std::string& property_name,
                                    const brillo::Any& property_value) {
  if (property_name != kIPConfigsProperty)
    return;

  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    LOG(ERROR) << "Device [" << device_path << "] not found";
    return;
  }

  auto* device = it->second->device();
  device->ipconfig = ParseIPConfigsProperty(device_path, property_value);

  // |device_added| will only be true if this method is called from the
  // registration callback, which in turn will only ever be called once per
  // device when it is first discovered. Deferring this callback until now
  // allows us to provide a Device struct populated with all the properties
  // available at the time.
  if (device_added) {
    for (auto& handler : device_added_handlers_)
      handler.Run(device);
  }

  // If this is the default device then notify the handlers.
  if (device_path == default_device_path_) {
    for (auto& handler : default_device_handlers_)
      handler.Run(device);
  }

  // Notify the handlers interested in all device changes.
  for (auto& handler : device_handlers_) {
    handler.Run(device);
  }
}

Client::IPConfig Client::ParseIPConfigsProperty(
    const std::string& device_path, const brillo::Any& property_value) {
  IPConfig ipconfig;
  auto paths = property_value.TryGet<std::vector<dbus::ObjectPath>>();
  if (paths.empty()) {
    LOG(WARNING) << "Device [" << device_path << "] has no IPConfigs";
    return ipconfig;
  }

  std::unique_ptr<IPConfigProxy> proxy;
  auto reset_proxy = [&](const dbus::ObjectPath& path) {
    if (proxy)
      proxy->ReleaseObjectProxy(base::DoNothing());

    if (path.IsValid())
      proxy.reset(new IPConfigProxy(bus_, path));
  };

  for (const auto& path : paths) {
    reset_proxy(path);
    brillo::VariantDictionary properties;
    if (!proxy->GetProperties(&properties, nullptr)) {
      // It is possible that an IPConfig object is removed after we know its
      // path, especially when the interface is going down.
      LOG(WARNING) << "Unable to get properties for IPConfig [" << path.value()
                   << "] on device [" << device_path << "]";
      continue;
    }

    // Detects the type of IPConfig. For ipv4 and ipv6 configurations, there
    // should be at most one non-empty entry for each type.
    std::string method = brillo::GetVariantValueOrDefault<std::string>(
        properties, kMethodProperty);
    if (method.empty()) {
      LOG(WARNING) << "Empty property [" << kMethodProperty << "] in IPConfig ["
                   << path.value() << "] on device [" << device_path << "]";
      continue;
    }

    // TODO(garrick): Replace use of method property with prefix length
    // inspection.
    const bool is_ipv4_type =
        (method == shill::kTypeIPv4 || method == shill::kTypeDHCP ||
         method == shill::kTypeBOOTP || method == shill::kTypeZeroConf);
    const bool is_ipv6_type = (method == shill::kTypeIPv6);
    if (!is_ipv4_type && !is_ipv6_type) {
      LOG(WARNING) << "Unknown type [" << method << "] in IPConfig ["
                   << path.value() << "] on device [" << device_path << "]";
      continue;
    }

    // While multiple IPv6 addresses are valid, we expect shill to provide at
    // most one for now.
    // TODO(garrick): Support multiple IPv6 configurations.
    if ((is_ipv4_type && !ipconfig.ipv4_address.empty()) ||
        (is_ipv6_type && !ipconfig.ipv6_address.empty())) {
      LOG(WARNING) << "Duplicate [" << method << "] IPConfig found"
                   << " on device [" << device_path << "]";
      continue;
    }

    std::string addr = brillo::GetVariantValueOrDefault<std::string>(
        properties, kAddressProperty);
    if (addr.empty()) {
      LOG(WARNING) << "Empty property [" << kAddressProperty
                   << "] in IPConfig [" << path.value() << "] on device ["
                   << device_path << "]";
      continue;
    }

    std::string gw = brillo::GetVariantValueOrDefault<std::string>(
        properties, kGatewayProperty);
    if (gw.empty()) {
      LOG(WARNING) << "Empty property [" << kGatewayProperty
                   << "] in IPConfig [" << path.value() << "] on device ["
                   << device_path << "]";
      continue;
    }

    int len =
        brillo::GetVariantValueOrDefault<int>(properties, kPrefixlenProperty);
    if (len <= 0) {
      LOG(WARNING) << "Empty property [" << kPrefixlenProperty
                   << "] in IPConfig [" << path.value() << "] on device ["
                   << device_path << "]";
      continue;
    }

    // TODO(garrick): Accommodate missing name servers.
    auto ns = brillo::GetVariantValueOrDefault<std::vector<std::string>>(
        properties, kNameServersProperty);
    if (ns.empty()) {
      LOG(WARNING) << "Empty property [" << kNameServersProperty
                   << "] in IPConfig [" << path.value() << "] on device ["
                   << device_path << "]";
      continue;
    }

    if (is_ipv4_type) {
      ipconfig.ipv4_prefix_length = len;
      ipconfig.ipv4_address = addr;
      ipconfig.ipv4_gateway = gw;
      ipconfig.ipv4_dns_addresses = ns;
    } else {  // is_ipv6_type
      ipconfig.ipv6_prefix_length = len;
      ipconfig.ipv6_address = addr;
      ipconfig.ipv6_gateway = gw;
      ipconfig.ipv6_dns_addresses = ns;
    }
  }
  reset_proxy(dbus::ObjectPath());

  return ipconfig;
}

}  // namespace shill
