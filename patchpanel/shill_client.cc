// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/shill_client.h"

#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>

namespace patchpanel {

namespace {

ShillClient::Device::Type ParseDeviceType(const std::string& type_str) {
  static const std::map<std::string, ShillClient::Device::Type> str2enum{
      {shill::kTypeCellular, ShillClient::Device::Type::kCellular},
      {shill::kTypeEthernet, ShillClient::Device::Type::kEthernet},
      {shill::kTypeEthernetEap, ShillClient::Device::Type::kEthernetEap},
      {shill::kTypeGuestInterface, ShillClient::Device::Type::kGuestInterface},
      {shill::kTypeLoopback, ShillClient::Device::Type::kLoopback},
      {shill::kTypePPP, ShillClient::Device::Type::kPPP},
      {shill::kTypePPPoE, ShillClient::Device::Type::kPPPoE},
      {shill::kTypeTunnel, ShillClient::Device::Type::kTunnel},
      {shill::kTypeWifi, ShillClient::Device::Type::kWifi},
      {shill::kTypeVPN, ShillClient::Device::Type::kVPN},
  };

  const auto it = str2enum.find(type_str);
  return it != str2enum.end() ? it->second
                              : ShillClient::Device::Type::kUnknown;
}

const std::string DeviceTypeName(ShillClient::Device::Type type) {
  static const std::map<ShillClient::Device::Type, std::string> enum2str{
      {ShillClient::Device::Type::kUnknown, "Unknown"},
      {ShillClient::Device::Type::kCellular, "Cellular"},
      {ShillClient::Device::Type::kEthernet, "Ethernet"},
      {ShillClient::Device::Type::kEthernetEap, "EthernetEap"},
      {ShillClient::Device::Type::kGuestInterface, "GuestInterface"},
      {ShillClient::Device::Type::kLoopback, "Loopback"},
      {ShillClient::Device::Type::kPPP, "PPP"},
      {ShillClient::Device::Type::kPPPoE, "PPPoE"},
      {ShillClient::Device::Type::kTunnel, "Tunnel"},
      {ShillClient::Device::Type::kVPN, "VPN"},
      {ShillClient::Device::Type::kWifi, "Wifi"},
  };

  const auto it = enum2str.find(type);
  return it != enum2str.end() ? it->second : "Unknown";
}

}  // namespace

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus) : bus_(bus) {
  manager_proxy_.reset(new org::chromium::flimflam::ManagerProxy(bus_));
  manager_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&ShillClient::OnManagerPropertyChange,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillClient::OnManagerPropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
}

const std::string& ShillClient::default_interface() const {
  return default_device_.ifname;
}

const std::set<std::string> ShillClient::get_devices() const {
  return devices_;
}

bool ShillClient::has_device(const std::string& ifname) const {
  return devices_.find(ifname) != devices_.end();
}

void ShillClient::ScanDevices() {
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

ShillClient::Device ShillClient::GetDefaultDevice() {
  brillo::VariantDictionary manager_props;

  if (!manager_proxy_->GetProperties(&manager_props, nullptr)) {
    LOG(ERROR) << "Unable to get manager properties";
    return {};
  }

  auto it = manager_props.find(shill::kDefaultServiceProperty);
  if (it == manager_props.end()) {
    LOG(ERROR) << "Manager properties is missing default service";
    return {};
  }

  dbus::ObjectPath service_path = it->second.TryGet<dbus::ObjectPath>();
  if (!service_path.IsValid() || service_path.value() == "/") {
    LOG(ERROR) << "Invalid DBus path for the default service";
    return {};
  }

  org::chromium::flimflam::ServiceProxy service_proxy(bus_, service_path);
  brillo::VariantDictionary service_props;
  if (!service_proxy.GetProperties(&service_props, nullptr)) {
    LOG(ERROR) << "Can't retrieve properties for default service"
               << service_path.value();
    return {};
  }

  it = service_props.find(shill::kIsConnectedProperty);
  if (it == service_props.end()) {
    LOG(ERROR) << "Service " << service_path.value() << " missing property "
               << shill::kIsConnectedProperty;
    return {};
  }

  if (!it->second.TryGet<bool>()) {
    LOG(INFO) << "Ignoring non-connected service " << service_path.value();
    return {};
  }

  std::string service_type = brillo::GetVariantValueOrDefault<std::string>(
      service_props, shill::kTypeProperty);
  if (service_type.empty()) {
    LOG(ERROR) << "Service " << service_path.value() << " missing property "
               << shill::kTypeProperty;
    return {};
  }

  Device device = {};
  device.type = ParseDeviceType(service_type);

  dbus::ObjectPath device_path =
      brillo::GetVariantValueOrDefault<dbus::ObjectPath>(
          service_props, shill::kDeviceProperty);
  if (!device_path.IsValid()) {
    LOG(ERROR) << "Service " << service_path.value()
               << " is missing device path";
    return {};
  }

  org::chromium::flimflam::DeviceProxy device_proxy(bus_, device_path);
  brillo::VariantDictionary device_props;
  if (!device_proxy.GetProperties(&device_props, nullptr)) {
    LOG(ERROR) << "Can't retrieve properties for device";
    return {};
  }

  device.ifname = brillo::GetVariantValueOrDefault<std::string>(
      device_props, shill::kInterfaceProperty);
  if (device.ifname.empty()) {
    LOG(ERROR) << "Device interface name is empty";
    return {};
  }

  device.service_path = service_path.value();
  return device;
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
  } else if (property_name != shill::kDefaultServiceProperty &&
             property_name != shill::kConnectionStateProperty) {
    return;
  }

  // All registered DefaultDeviceChangeHandler objects should be called if
  // the default network has changed or if shill::kDevicesProperty has changed.
  SetDefaultDevice(GetDefaultDevice());
}

void ShillClient::SetDefaultDevice(const Device& new_default) {
  if (default_device_.ifname == new_default.ifname)
    return;

  LOG(INFO) << "Default device changed from " << default_device_ << " to "
            << new_default;

  for (const auto& h : default_device_handlers_) {
    if (!h.is_null())
      h.Run(new_default, default_device_);
  }
  default_device_ = new_default;
}

void ShillClient::RegisterDefaultDeviceChangedHandler(
    const DefaultDeviceChangeHandler& handler) {
  default_device_handlers_.emplace_back(handler);
  // Explicitly trigger the callback once to let it know of the the current
  // default interface. The previous interface is left empty.
  handler.Run(default_device_, {});
}

void ShillClient::RegisterDevicesChangedHandler(
    const DevicesChangeHandler& handler) {
  device_handlers_.emplace_back(handler);
}

void ShillClient::RegisterIPConfigsChangedHandler(
    const IPConfigsChangeHandler& handler) {
  ipconfigs_handlers_.emplace_back(handler);
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

    // Registers handler if we see this device for the first time.
    if (known_device_paths_.insert(std::make_pair(device, path)).second) {
      org::chromium::flimflam::DeviceProxy proxy(bus_, path);
      proxy.RegisterPropertyChangedSignalHandler(
          base::Bind(&ShillClient::OnDevicePropertyChange,
                     weak_factory_.GetWeakPtr(), device),
          base::Bind(&ShillClient::OnDevicePropertyChangeRegistration,
                     weak_factory_.GetWeakPtr()));
      known_device_paths_[device] = path;
    }
  }

  for (const auto& d : devices_) {
    if (new_devices.find(d) == new_devices.end())
      removed.insert(d);
  }

  devices_ = new_devices;

  for (const auto& h : device_handlers_)
    h.Run(added, removed);
}

ShillClient::IPConfig ShillClient::ParseIPConfigsProperty(
    const std::string& device, const brillo::Any& property_value) {
  IPConfig ipconfig;
  for (const auto& path :
       property_value.TryGet<std::vector<dbus::ObjectPath>>()) {
    std::unique_ptr<org::chromium::flimflam::IPConfigProxy> ipconfig_proxy(
        new org::chromium::flimflam::IPConfigProxy(bus_, path));
    brillo::VariantDictionary ipconfig_props;

    if (!ipconfig_proxy->GetProperties(&ipconfig_props, nullptr)) {
      // It is possible that an IPConfig object is removed after we know its
      // path, especially when the interface is going down.
      LOG(WARNING) << "[" << device << "]: "
                   << "Unable to get properties for " << path.value();
      continue;
    }

    // Detects the type of IPConfig. For ipv4 and ipv6 configurations, there
    // should be at most one for each type.
    auto it = ipconfig_props.find(shill::kMethodProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device << "]: "
                   << "IPConfig properties is missing Method";
      continue;
    }
    const std::string& method = it->second.TryGet<std::string>();
    const bool is_ipv4_type =
        (method == shill::kTypeIPv4 || method == shill::kTypeDHCP ||
         method == shill::kTypeBOOTP || method == shill::kTypeZeroConf);
    const bool is_ipv6_type = (method == shill::kTypeIPv6);
    if (!is_ipv4_type && !is_ipv6_type) {
      LOG(WARNING) << "[" << device << "]: "
                   << "unknown type \"" << method << "\" for " << path.value();
      continue;
    }
    if ((is_ipv4_type && !ipconfig.ipv4_address.empty()) ||
        (is_ipv6_type && !ipconfig.ipv6_address.empty())) {
      LOG(WARNING) << "[" << device << "]: "
                   << "Duplicated ipconfig for " << method;
      continue;
    }

    // Gets the value of address, prefix_length, gateway, and dns_servers.
    it = ipconfig_props.find(shill::kAddressProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device << "]: "
                   << "IPConfig properties is missing Address";
      continue;
    }
    const std::string& address = it->second.TryGet<std::string>();

    it = ipconfig_props.find(shill::kPrefixlenProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device << "]: "
                   << "IPConfig properties is missing Prefixlen";
      continue;
    }
    int prefix_length = it->second.TryGet<int>();

    it = ipconfig_props.find(shill::kGatewayProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device << "]: "
                   << "IPConfig properties is missing Gateway";
      continue;
    }
    const std::string& gateway = it->second.TryGet<std::string>();

    it = ipconfig_props.find(shill::kNameServersProperty);
    if (it == ipconfig_props.end()) {
      LOG(WARNING) << "[" << device << "]: "
                   << "IPConfig properties is missing NameServers";
      // Shill will emit this property with empty value if it has no dns for
      // this device, so missing this property indicates an error.
      continue;
    }
    const std::vector<std::string>& dns_addresses =
        it->second.TryGet<std::vector<std::string>>();

    // Checks if this ipconfig is valid: address, gateway, and prefix_length
    // should not be empty.
    if (address.empty() || gateway.empty() || prefix_length == 0) {
      LOG(WARNING) << "[" << device << "]: "
                   << "Skipped invalid ipconfig: "
                   << "address.length()=" << address.length()
                   << ", gateway.length()=" << gateway.length()
                   << ", prefix_length=" << prefix_length;
      continue;
    }

    // Fills the IPConfig struct according to the type.
    if (is_ipv4_type) {
      ipconfig.ipv4_prefix_length = prefix_length;
      ipconfig.ipv4_address = address;
      ipconfig.ipv4_gateway = gateway;
      ipconfig.ipv4_dns_addresses = dns_addresses;
    } else {  // is_ipv6_type
      ipconfig.ipv6_prefix_length = prefix_length;
      ipconfig.ipv6_address = address;
      ipconfig.ipv6_gateway = gateway;
      ipconfig.ipv6_dns_addresses = dns_addresses;
    }
  }

  return ipconfig;
}

bool ShillClient::GetDeviceProperties(const std::string& device,
                                      Device* output) {
  DCHECK(output);
  const auto& device_it = known_device_paths_.find(device);
  if (device_it == known_device_paths_.end()) {
    LOG(ERROR) << "Unknown device " << device;
    return false;
  }

  org::chromium::flimflam::DeviceProxy proxy(bus_, device_it->second);
  brillo::VariantDictionary props;
  if (!proxy.GetProperties(&props, nullptr)) {
    LOG(WARNING) << "Unable to get device properties for " << device;
    return false;
  }

  const auto& type_it = props.find(shill::kTypeProperty);
  if (type_it == props.end()) {
    LOG(WARNING) << "Device properties is missing Type for " << device;
    return false;
  }
  const std::string& type_str = type_it->second.TryGet<std::string>();
  output->type = ParseDeviceType(type_str);
  if (output->type == Device::Type::kUnknown)
    LOG(WARNING) << "Unknown device type " << type_str << " for " << device;

  const auto& interface_it = props.find(shill::kInterfaceProperty);
  if (interface_it == props.end()) {
    LOG(WARNING) << "Device properties is missing Interface for " << device;
    return false;
  }
  output->ifname = interface_it->second.TryGet<std::string>();

  const auto& ipconfigs_it = props.find(shill::kIPConfigsProperty);
  if (ipconfigs_it == props.end()) {
    LOG(WARNING) << "Device properties is missing IPConfigs for " << device;
    return false;
  }
  output->ipconfig = ParseIPConfigsProperty(device, ipconfigs_it->second);

  return true;
}

void ShillClient::OnDevicePropertyChangeRegistration(
    const std::string& interface,
    const std::string& signal_name,
    bool success) {
  if (!success)
    LOG(ERROR) << "[" << interface << "]: "
               << "Unable to register listener for " << signal_name;
}

void ShillClient::OnDevicePropertyChange(const std::string& device,
                                         const std::string& property_name,
                                         const brillo::Any& property_value) {
  if (property_name != shill::kIPConfigsProperty)
    return;

  const IPConfig& ipconfig = ParseIPConfigsProperty(device, property_value);
  // TODO(jiejiang): Keep a cache of the last parsed IPConfig, and only
  // trigger handlers if there is an actual change.
  for (const auto& handler : ipconfigs_handlers_)
    handler.Run(device, ipconfig);
}

std::ostream& operator<<(std::ostream& stream, const ShillClient::Device& dev) {
  return stream << "{ifname: " << dev.ifname
                << ", type: " << DeviceTypeName(dev.type)
                << ", service: " << dev.service_path << "}";
}

}  // namespace patchpanel
