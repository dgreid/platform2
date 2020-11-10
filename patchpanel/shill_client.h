// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_SHILL_CLIENT_H_
#define PATCHPANEL_SHILL_CLIENT_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <shill/dbus-proxies.h>

namespace patchpanel {

// Listens for shill signals over dbus in order to:
// - Figure out which network interface (if any) is being used as the default
//   service.
// - Invoke callbacks when the IPConfigs of a device has changed.
class ShillClient {
 public:
  // IPConfig for a device. If the device does not have a valid ipv4/ipv6
  // config, the corresponding fields will be empty or 0.
  // TODO(jiejiang): add the following fields into this struct:
  // - IPv4 search domains
  // - IPv6 search domains
  // - MTU (one only per network)
  struct IPConfig {
    int ipv4_prefix_length;
    std::string ipv4_address;
    std::string ipv4_gateway;
    std::vector<std::string> ipv4_dns_addresses;

    int ipv6_prefix_length;
    // Note due to the limitation of shill, we will only get one IPv6 address
    // from it. This address should be the privacy address for device with type
    // of ethernet or wifi.
    std::string ipv6_address;
    std::string ipv6_gateway;
    std::vector<std::string> ipv6_dns_addresses;
  };

  // Represents the properties of an object of org.chromium.flimflam.Device.
  // Only contains the properties we care about.
  // TODO(jiejiang): add the following fields into this struct:
  // - the DBus path of the Service associated to this Device if any
  // - the connection state of the Service, if possible by translating back to
  //   the enum shill::Service::ConnectState
  struct Device {
    // A subset of shill::Technology::Type.
    enum class Type {
      kUnknown,
      kCellular,
      kEthernet,
      kEthernetEap,
      kGuestInterface,
      kLoopback,
      kPPP,
      kPPPoE,
      kTunnel,
      kVPN,
      kWifi,
    };

    Type type;
    std::string ifname;
    IPConfig ipconfig;
  };

  using DefaultInterfaceChangeHandler = base::Callback<void(
      const std::string& new_ifname, const std::string& prev_ifname)>;
  using DevicesChangeHandler =
      base::Callback<void(const std::set<std::string>& added,
                          const std::set<std::string>& removed)>;
  using IPConfigsChangeHandler =
      base::Callback<void(const std::string& device, const IPConfig& ipconfig)>;

  explicit ShillClient(const scoped_refptr<dbus::Bus>& bus);
  ShillClient(const ShillClient&) = delete;
  ShillClient& operator=(const ShillClient&) = delete;

  virtual ~ShillClient() = default;

  void RegisterDefaultInterfaceChangedHandler(
      const DefaultInterfaceChangeHandler& handler);

  void RegisterDevicesChangedHandler(const DevicesChangeHandler& handler);

  void RegisterIPConfigsChangedHandler(const IPConfigsChangeHandler& handler);

  void ScanDevices();

  // Fetches device properties via dbus. Returns false if an error occurs. Notes
  // that this method will block the current thread.
  virtual bool GetDeviceProperties(const std::string& device, Device* output);

  // Returns the cached interface name; does not initiate a property fetch.
  virtual const std::string& default_interface() const;
  // Returns interface names of all known shill Devices.
  const std::set<std::string> get_devices() const;
  // Returns true if |ifname| is a known shill Device.
  bool has_device(const std::string& ifname) const;

 protected:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  void OnDevicePropertyChangeRegistration(const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);
  void OnDevicePropertyChange(const std::string& device,
                              const std::string& property_name,
                              const brillo::Any& property_value);

  // Returns the name of the default interface for the system, or an empty
  // string when the system has no default interface.
  virtual std::string GetDefaultInterface();

 private:
  void UpdateDevices(const brillo::Any& property_value);

  // Sets the internal variable tracking the system default interface and calls
  // the default interface handler if the default interface changed. When the
  // default interface is lost and a fallback exists, the fallback is used
  // instead. Returns the previous default interface.
  std::string SetDefaultInterface(std::string new_default);

  // Parses the |property_value| as the IPConfigs property of |device|, which
  // should be a list of object paths of IPConfigs.
  IPConfig ParseIPConfigsProperty(const std::string& device,
                                  const brillo::Any& property_value);

  // Tracks the name of the system default interface chosen by shill.
  std::string default_interface_;
  // Another network interface on the system to use as a possible fallback if
  // no system default interface exists.
  std::string fallback_default_interface_;
  // Tracks all network interfaces managed by shill.
  std::set<std::string> devices_;
  // Stores the map from interface to its object path in shill for all the
  // devices we have seen. Unlike |devices_|, entries in this map will never
  // be removed during the lifetime of this class. We maintain this map mainly
  // for keeping track of the device object proxies we have created, to avoid
  // registering the handler on the same object twice.
  std::map<std::string, dbus::ObjectPath> known_device_paths_;

  // Called when the interface used as the default interface changes.
  std::vector<DefaultInterfaceChangeHandler> default_interface_handlers_;
  // Called when the list of network interfaces managed by shill changes.
  std::vector<DevicesChangeHandler> device_handlers_;
  // Called when the IPConfigs of any device changes.
  std::vector<IPConfigsChangeHandler> ipconfigs_handlers_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_SHILL_CLIENT_H_
