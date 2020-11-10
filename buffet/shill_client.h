// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_SHILL_CLIENT_H_
#define BUFFET_SHILL_CLIENT_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <shill/dbus-proxies.h>
#include <weave/provider/network.h>
#include <weave/provider/wifi.h>

namespace buffet {

class ShillClient final : public weave::provider::Network,
                          public weave::provider::Wifi {
 public:
  ShillClient(const scoped_refptr<dbus::Bus>& bus,
              const std::set<std::string>& device_whitelist,
              bool disable_xmpp);
  ShillClient(const ShillClient&) = delete;
  ShillClient& operator=(const ShillClient&) = delete;

  ~ShillClient();

  void Init();

  // NetworkProvider implementation.
  void AddConnectionChangedCallback(
      const ConnectionChangedCallback& listener) override;
  State GetConnectionState() const override;
  void OpenSslSocket(const std::string& host,
                     uint16_t port,
                     const OpenSslSocketCallback& callback) override;

  // WifiProvider implementation.
  void Connect(const std::string& ssid,
               const std::string& passphrase,
               const weave::DoneCallback& callback) override;
  void StartAccessPoint(const std::string& ssid) override;
  void StopAccessPoint() override;

  std::string GetIpAddress();

 private:
  struct DeviceState {
    std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface> device;
    // ServiceProxy objects are shared because the connecting service will
    // also be the selected service for a device, but is not always the selected
    // service (for instance, in the period between configuring a WiFi service
    // with credentials, and when Connect() is called.)
    std::shared_ptr<org::chromium::flimflam::ServiceProxy> selected_service;
    State service_state{State::kOffline};
  };

  bool IsMonitoredDevice(org::chromium::flimflam::DeviceProxyInterface* device);
  void OnShillServiceOwnerChange(const std::string& old_owner,
                                 const std::string& new_owner);
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);
  void OnDevicePropertyChangeRegistration(const dbus::ObjectPath& device_path,
                                          const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);
  void OnDevicePropertyChange(const dbus::ObjectPath& device_path,
                              const std::string& property_name,
                              const brillo::Any& property_value);
  void OnServicePropertyChangeRegistration(const dbus::ObjectPath& path,
                                           const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnServicePropertyChange(const dbus::ObjectPath& service_path,
                               const std::string& property_name,
                               const brillo::Any& property_value);

  void OnStateChangeForConnectingService(const std::string& state);
  void OnErrorChangeForConnectingService(const std::string& error);
  void OnStrengthChangeForConnectingService(uint8_t signal_strength);
  void OnStateChangeForSelectedService(const dbus::ObjectPath& service_path,
                                       const std::string& state);
  void OnIpConfigChange(const dbus::ObjectPath& ip_config_path,
                        const std::string& device_path);

  void UpdateConnectivityState();
  void NotifyConnectivityListeners(bool am_online);
  // Clean up state related to a connecting service.
  void CleanupConnectingService();

  void ConnectToServiceError(
      std::shared_ptr<org::chromium::flimflam::ServiceProxy>
          connecting_service);

  const scoped_refptr<dbus::Bus> bus_;
  org::chromium::flimflam::ManagerProxy manager_proxy_;
  // There is logic that assumes we will never change this device list
  // in OnManagerPropertyChange.  Do not be tempted to remove this const.
  const std::set<std::string> device_whitelist_;
  bool disable_xmpp_{false};
  std::vector<ConnectionChangedCallback> connectivity_listeners_;

  // State for tracking where we are in our attempts to connect to a service.
  bool have_called_connect_{false};
  std::shared_ptr<org::chromium::flimflam::ServiceProxy> connecting_service_;
  std::string connecting_service_error_;
  weave::DoneCallback connect_done_callback_;

  // State for tracking our online connectivity.
  std::map<dbus::ObjectPath, DeviceState> devices_;
  State connectivity_state_{State::kOffline};

  std::string ip_address_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};
};

}  // namespace buffet

#endif  // BUFFET_SHILL_CLIENT_H_
