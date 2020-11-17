// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_CLIENT_CLIENT_H_
#define SHILL_DBUS_CLIENT_CLIENT_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind_helpers.h>
#include <base/callback_forward.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <brillo/brillo_export.h>
#include <shill/dbus-proxies.h>
#include <chromeos/dbus/service_constants.h>

namespace shill {

// Shill D-Bus client for listening to common manager, service and device
// properties. This class is the result of an effort to consolidate a lot of
// duplicated boilerplate across multiple platform2 packages.
// TODO(garrick): Integrate into applicable platform2 packages.
class BRILLO_EXPORT Client {
 public:
  // IPConfig for a device. If the device does not have a valid ipv4/ipv6
  // config, the corresponding fields will be empty or 0.
  // TODO(jiejiang): add the following fields into this struct:
  // - IPv4 search domains
  // - IPv6 search domains
  // - MTU (one only per network)
  struct IPConfig {
    bool operator==(const IPConfig& that) {
      return this->ipv4_prefix_length == that.ipv4_prefix_length &&
             this->ipv4_address == that.ipv4_address &&
             this->ipv4_gateway == that.ipv4_gateway &&
             this->ipv4_dns_addresses == that.ipv4_dns_addresses &&
             this->ipv6_prefix_length == that.ipv6_prefix_length &&
             this->ipv6_address == that.ipv6_address &&
             this->ipv6_gateway == that.ipv6_gateway &&
             this->ipv6_dns_addresses == that.ipv6_dns_addresses;
    }

    int ipv4_prefix_length;
    std::string ipv4_address;
    std::string ipv4_gateway;
    std::vector<std::string> ipv4_dns_addresses;

    int ipv6_prefix_length;
    // Note due to the limitation of shill, we will only get one IPv6 address
    // from it. This address should be the privacy address for device with type
    // of ethernet or wifi.
    // TODO(garrick): Support multiple IPv6 configurations.
    std::string ipv6_address;
    std::string ipv6_gateway;
    std::vector<std::string> ipv6_dns_addresses;
  };

  // Represents a subset of properties from org.chromium.flimflam.Device.
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

    bool operator==(const Device& that) {
      return this->type == that.type && this->ifname == that.ifname &&
             this->ipconfig == that.ipconfig;
    }

    Type type;
    std::string ifname;
    IPConfig ipconfig;
  };

  using DefaultServiceChangedHandler = base::Callback<void()>;
  using DeviceChangedHandler = base::Callback<void(const Device* const)>;

  explicit Client(scoped_refptr<dbus::Bus> bus);
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  // Initiates the connection to DBus and starts processing signals.
  void Init();

  // |handler| will be invoked whenever the default service changes, i.e.
  // whenever the default service switches from "none" to a valid path or
  // vice-versa.
  // Multiple handlers may be registered.
  void RegisterDefaultServiceChangedHandler(
      const DefaultServiceChangedHandler& handler);

  // |handler| will be invoked whenever the device associated with the default
  // service changes. The following changes will triggers this handler:
  // * The default service itself changes,
  // * The default service is connected or disconnected,
  // * The device connected to the default service changes,
  // * The IP configuration of the default device changes.
  //
  // If the default service is disconnected, the device will be null.
  // Multiple handlers may be registered.
  void RegisterDefaultDeviceChangedHandler(const DeviceChangedHandler& handler);

  // |handler| will be invoked whenever there is a change to a tracked property
  // within the configuration of a device; currently only IPConfig properties
  // are tracked.
  // Multiple handlers may be registered.
  void RegisterDeviceChangedHandler(const DeviceChangedHandler& handler);

  // |handler| will be invoked whenever a device is added or removed from shill.
  // Note that if the default service switches to VPN, the corresponding device
  // will be added and tracked. This will not occur for any other type of
  // virtual device. Handlers can use |Device.type| to filter, if necessary.
  // Multiple handlers may be registered.
  void RegisterDeviceAddedHandler(const DeviceChangedHandler& handler);
  void RegisterDeviceRemovedHandler(const DeviceChangedHandler& handler);

 protected:
  // All of the methods and members with protected access scope are needed for
  // unit testing.

  // Invoked when the DBus service owner name changes, which occurs when the
  // service is stopped (new_owner is empty) or restarted (new_owner !=
  // old_owner)
  // This will trigger any existing proxies to the existing service to be reset,
  // and a new manager proxy will be established.
  void OnOwnerChange(const std::string& old_owner,
                     const std::string& new_owner);

  // This callback is invoked whenever a manager property change signal is
  // received; if the property is one we pay attention to the corresponding
  // Handle*Changed handler will be called.
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  // This callback is invoked whenever the default service property change
  // signal is received; if the property is one we pay attention to the
  // corresponding Handler*Changed handler will be called.
  void OnDefaultServicePropertyChange(const std::string& property_name,
                                      const brillo::Any& property_value);

  // This callback is invoked whenever a device property change signal is
  // received; if the property is one we pay attention to the corresponding
  // handler will be invoked. If the device is new, it will be added to the
  // internal list that are tracked.
  void OnDevicePropertyChange(bool device_added,
                              const std::string& device_path,
                              const std::string& property_name,
                              const brillo::Any& property_value);

  // Methods for managing proxy objects. These are overridden in tests to ensure
  // registration hooks, callbacks and properties can be plumbed back through
  // the interfaces as needed.
  virtual void NewManagerProxy();
  virtual void ReleaseManagerProxy();
  virtual void NewDefaultServiceProxy(const dbus::ObjectPath& service_path);
  virtual void ReleaseDefaultServiceProxy();
  virtual std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>
  NewDeviceProxy(const dbus::ObjectPath& device_path);

  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      manager_proxy_;
  std::unique_ptr<org::chromium::flimflam::ServiceProxyInterface>
      default_service_proxy_;

 private:
  // This callback is invoked whenever the default service changes, that is,
  // when it switches from one service to another. If applicable, the callback
  // set via RegisterDefaultServiceChangedHandler will be invoked.
  void HandleDefaultServiceChanged(const brillo::Any& property_value);

  // This callback is invoked whenever the (physical) device list provided by
  // shill changes.
  void HandleDevicesChanged(const brillo::Any& property_value);

  // This callback is invoked whenever a new manager proxy is created. It will
  // trigger the discovery of the default service.
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);

  // This callback is invoked whenever a new default service proxy is created.
  // It will trigger the discovery of the device associated with the default
  // service.
  void OnDefaultServicePropertyChangeRegistration(
      const std::string& interface,
      const std::string& signal_name,
      bool success);

  // This callback is invoked whenever a new device proxy is created. It will
  // trigger the discovery of the device properties we care about including its
  // type, interface name and IP configuration.
  void OnDevicePropertyChangeRegistration(const std::string& device_path,
                                          const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);

  void SetupManagerProxy();
  void SetupDefaultServiceProxy(const dbus::ObjectPath& service_path);
  void SetupDeviceProxy(const dbus::ObjectPath& device_path);

  // Wraps a device with its DBus proxy on which property change signals are
  // received.
  class DeviceWrapper {
   public:
    DeviceWrapper(
        scoped_refptr<dbus::Bus> bus,
        std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> proxy)
        : bus_(bus), proxy_(std::move(proxy)) {}
    ~DeviceWrapper() {
      bus_->RemoveObjectProxy(kFlimflamServiceName, proxy_->GetObjectPath(),
                              base::DoNothing());
    }
    DeviceWrapper(const DeviceWrapper&) = delete;
    DeviceWrapper& operator=(const DeviceWrapper&) = delete;

    Device* device() { return &device_; }
    org::chromium::flimflam::DeviceProxyInterface* proxy() {
      return proxy_.get();
    }

   private:
    scoped_refptr<dbus::Bus> bus_;
    Device device_;
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> proxy_;
  };

  void AddDevice(const dbus::ObjectPath& path);

  // Reads the list of IPConfigs for a device and composes them into an IPConfig
  // data structure.
  IPConfig ParseIPConfigsProperty(const std::string& device_path,
                                  const brillo::Any& property_value);

  scoped_refptr<dbus::Bus> bus_;

  std::vector<DefaultServiceChangedHandler> default_service_handlers_;
  std::vector<DeviceChangedHandler> default_device_handlers_;
  std::vector<DeviceChangedHandler> device_handlers_;
  std::vector<DeviceChangedHandler> device_added_handlers_;
  std::vector<DeviceChangedHandler> device_removed_handlers_;

  bool default_service_connected_ = false;
  std::string default_device_path_;

  // Tracked devices keyed by path.
  std::map<std::string, std::unique_ptr<DeviceWrapper>> devices_;

  base::WeakPtrFactory<Client> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_CLIENT_CLIENT_H_
