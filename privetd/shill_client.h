// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIVETD_SHILL_CLIENT_H_
#define PRIVETD_SHILL_CLIENT_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/cancelable_callback.h>
#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>

#include "shill/dbus-proxies.h"

namespace privetd {

enum class ServiceState {
  kOffline = 0,
  kFailure,
  kConnecting,
  kConnected,
};

std::string ServiceStateToString(ServiceState state);

class ShillClient final {
 public:
  // A callback that interested parties can register to be notified of
  // transitions from online to offline and vice versa.  The boolean
  // parameter will be true if we're online, and false if we're offline.
  using ConnectivityListener = base::Callback<void(bool)>;

  ShillClient(const scoped_refptr<dbus::Bus>& bus,
              const std::set<std::string>& device_whitelist);
  ~ShillClient() = default;


  void Init();
  void RegisterConnectivityListener(const ConnectivityListener& listener);
  // Causes shill to attempt to connect to the given network with the given
  // passphrase.  This is accomplished by:
  //  1) Configuring a service through the Manager with the SSID and passphrase.
  //  2) Calling Connect() on the service.
  //  2) Monitoring the returned Service object until we reach an online state,
  //     an error state, or another call to ConnectToService() occurs.
  // Returns false on immediate failures with some descriptive codes in |error|.
  bool ConnectToService(const std::string& ssid,
                        const std::string& passphrase,
                        const base::Closure& on_success,
                        chromeos::ErrorPtr* error);
  ServiceState GetConnectionState() const;
  bool AmOnline() const;

 private:
  struct DeviceState {
    std::unique_ptr<org::chromium::flimflam::DeviceProxy> device;
    // ServiceProxy objects are shared because the connecting service will
    // also be the selected service for a device, but is not always the selected
    // service (for instance, in the period between configuring a WiFi service
    // with credentials, and when Connect() is called.)
    std::shared_ptr<org::chromium::flimflam::ServiceProxy> selected_service;
    ServiceState service_state{ServiceState::kOffline};
  };

  bool IsMonitoredDevice(org::chromium::flimflam::DeviceProxy* device);
  void OnShillServiceOwnerChange(const std::string& old_owner,
                                 const std::string& new_owner);
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const chromeos::Any& property_value);
  void OnDevicePropertyChangeRegistration(const dbus::ObjectPath& device_path,
                                          const std::string& interface,
                                          const std::string& signal_name,
                                          bool success);
  void OnDevicePropertyChange(const dbus::ObjectPath& device_path,
                              const std::string& property_name,
                              const chromeos::Any& property_value);
  void OnServicePropertyChangeRegistration(const dbus::ObjectPath& path,
                                           const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnServicePropertyChange(const dbus::ObjectPath& service_path,
                               const std::string& property_name,
                               const chromeos::Any& property_value);

  void OnStateChangeForConnectingService(const dbus::ObjectPath& service_path,
                                         const std::string& state);
  void OnStrengthChangeForConnectingService(
      const dbus::ObjectPath& service_path,
      uint8_t signal_strength);
  void OnStateChangeForSelectedService(const dbus::ObjectPath& service_path,
                                       const std::string& state);
  void UpdateConnectivityState();
  void NotifyConnectivityListeners(bool am_online);
  // Clean up state related to a connecting service.  If
  // |check_for_reset_pending| is set, then we'll check to see if we've called
  // ConnectToService() in the time since a task to call this function was
  // posted.
  void CleanupConnectingService(bool check_for_reset_pending);

  const scoped_refptr<dbus::Bus> bus_;
  org::chromium::flimflam::ManagerProxy manager_proxy_;
  // There is logic that assumes we will never change this device list
  // in OnManagerPropertyChange.  Do not be tempted to remove this const.
  const std::set<std::string> device_whitelist_;
  std::vector<ConnectivityListener> connectivity_listeners_;

  // State for tracking where we are in our attempts to connect to a service.
  bool connecting_service_reset_pending_{false};
  bool have_called_connect_{false};
  std::shared_ptr<org::chromium::flimflam::ServiceProxy> connecting_service_;
  base::CancelableClosure on_connect_success_;

  // State for tracking our online connectivity.
  std::map<dbus::ObjectPath, DeviceState> devices_;
  ServiceState connectivity_state_{ServiceState::kOffline};

  base::WeakPtrFactory<ShillClient> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(ShillClient);
};

}  // namespace privetd

#endif  // PRIVETD_SHILL_CLIENT_H_
