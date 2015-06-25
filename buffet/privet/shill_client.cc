// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/privet/shill_client.h"

#include <set>

#include <base/message_loop/message_loop.h>
#include <base/stl_util.h>
#include <chromeos/any.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/errors/error.h>
#include <chromeos/errors/error_codes.h>

using chromeos::Any;
using chromeos::VariantDictionary;
using dbus::ObjectPath;
using org::chromium::flimflam::DeviceProxy;
using org::chromium::flimflam::ServiceProxy;
using std::map;
using std::set;
using std::string;
using std::vector;

namespace privetd {

namespace {

void IgnoreDetachEvent() {
}

bool GetStateForService(ServiceProxy* service, string* state) {
  CHECK(service) << "|service| was nullptr in GetStateForService()";
  VariantDictionary properties;
  if (!service->GetProperties(&properties, nullptr)) {
    LOG(WARNING) << "Failed to read properties from service.";
    return false;
  }
  auto property_it = properties.find(shill::kStateProperty);
  if (property_it == properties.end()) {
    LOG(WARNING) << "No state found in service properties.";
    return false;
  }
  string new_state = property_it->second.TryGet<string>();
  if (new_state.empty()) {
    LOG(WARNING) << "Invalid state value.";
    return false;
  }
  *state = new_state;
  return true;
}

ServiceState ShillServiceStateToServiceState(const string& state) {
  // TODO(wiley) What does "unconfigured" mean in a world with multiple sets
  //             of WiFi credentials?
  // TODO(wiley) Detect disabled devices, update state appropriately.
  if ((state.compare(shill::kStateReady) == 0) ||
      (state.compare(shill::kStatePortal) == 0) ||
      (state.compare(shill::kStateOnline) == 0)) {
    return ServiceState::kConnected;
  }
  if ((state.compare(shill::kStateAssociation) == 0) ||
      (state.compare(shill::kStateConfiguration) == 0)) {
    return ServiceState::kConnecting;
  }
  if ((state.compare(shill::kStateFailure) == 0) ||
      (state.compare(shill::kStateActivationFailure) == 0)) {
    // TODO(wiley) Get error information off the service object.
    return ServiceState::kFailure;
  }
  if ((state.compare(shill::kStateIdle) == 0) ||
      (state.compare(shill::kStateOffline) == 0) ||
      (state.compare(shill::kStateDisconnect) == 0)) {
    return ServiceState::kOffline;
  }
  LOG(WARNING) << "Unknown state found: '" << state << "'";
  return ServiceState::kOffline;
}

}  // namespace

std::string ServiceStateToString(ServiceState state) {
  switch (state) {
    case ServiceState::kOffline:
      return "offline";
    case ServiceState::kFailure:
      return "failure";
    case ServiceState::kConnecting:
      return "connecting";
    case ServiceState::kConnected:
      return "connected";
  }
  LOG(ERROR) << "Unknown ServiceState value!";
  return "unknown";
}

ShillClient::ShillClient(const scoped_refptr<dbus::Bus>& bus,
                         const set<string>& device_whitelist)
    : bus_{bus},
      manager_proxy_{bus_, ObjectPath{"/"}},
      device_whitelist_{device_whitelist} {
  manager_proxy_.RegisterPropertyChangedSignalHandler(
      base::Bind(&ShillClient::OnManagerPropertyChange,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&ShillClient::OnManagerPropertyChangeRegistration,
                 weak_factory_.GetWeakPtr()));
  auto owner_changed_cb = base::Bind(&ShillClient::OnShillServiceOwnerChange,
                                     weak_factory_.GetWeakPtr());
  bus_->GetObjectProxy(shill::kFlimflamServiceName, ObjectPath{"/"})
      ->SetNameOwnerChangedCallback(owner_changed_cb);
}

void ShillClient::Init() {
  VLOG(2) << "ShillClient::Init();";
  CleanupConnectingService(false);
  devices_.clear();
  connectivity_state_ = ServiceState::kOffline;
  VariantDictionary properties;
  if (!manager_proxy_.GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties from Manager, waiting for "
                  "Manager to come back online.";
    return;
  }
  auto it = properties.find(shill::kDevicesProperty);
  CHECK(it != properties.end()) << "shill should always publish a device list.";
  OnManagerPropertyChange(shill::kDevicesProperty, it->second);
}

bool ShillClient::ConnectToService(const string& ssid,
                                   const string& passphrase,
                                   const base::Closure& on_success,
                                   chromeos::ErrorPtr* error) {
  CleanupConnectingService(false);
  VariantDictionary service_properties;
  service_properties[shill::kTypeProperty] = Any{string{shill::kTypeWifi}};
  service_properties[shill::kSSIDProperty] = Any{ssid};
  service_properties[shill::kPassphraseProperty] = Any{passphrase};
  service_properties[shill::kSecurityProperty] = Any{
      string{passphrase.empty() ? shill::kSecurityNone : shill::kSecurityPsk}};
  service_properties[shill::kSaveCredentialsProperty] = Any{true};
  service_properties[shill::kAutoConnectProperty] = Any{true};
  ObjectPath service_path;
  if (!manager_proxy_.ConfigureService(service_properties, &service_path,
                                       error)) {
    return false;
  }
  if (!manager_proxy_.RequestScan(shill::kTypeWifi, error)) {
    return false;
  }
  connecting_service_.reset(new ServiceProxy{bus_, service_path});
  on_connect_success_.Reset(on_success);
  connecting_service_->RegisterPropertyChangedSignalHandler(
      base::Bind(&ShillClient::OnServicePropertyChange,
                 weak_factory_.GetWeakPtr(), service_path),
      base::Bind(&ShillClient::OnServicePropertyChangeRegistration,
                 weak_factory_.GetWeakPtr(), service_path));
  return true;
}

ServiceState ShillClient::GetConnectionState() const {
  return connectivity_state_;
}

bool ShillClient::AmOnline() const {
  return connectivity_state_ == ServiceState::kConnected;
}

void ShillClient::RegisterConnectivityListener(
    const ConnectivityListener& listener) {
  connectivity_listeners_.push_back(listener);
}

bool ShillClient::IsMonitoredDevice(DeviceProxy* device) {
  if (device_whitelist_.empty()) {
    return true;
  }
  VariantDictionary device_properties;
  if (!device->GetProperties(&device_properties, nullptr)) {
    LOG(ERROR) << "Devices without properties aren't whitelisted.";
    return false;
  }
  auto it = device_properties.find(shill::kInterfaceProperty);
  if (it == device_properties.end()) {
    LOG(ERROR) << "Failed to find interface property in device properties.";
    return false;
  }
  return ContainsKey(device_whitelist_, it->second.TryGet<string>());
}

void ShillClient::OnShillServiceOwnerChange(const string& old_owner,
                                            const string& new_owner) {
  VLOG(1) << "Shill service owner name changed to '" << new_owner << "'";
  if (new_owner.empty()) {
    CleanupConnectingService(false);
    devices_.clear();
    connectivity_state_ = ServiceState::kOffline;
  } else {
    Init();  // New service owner means shill reset!
  }
}

void ShillClient::OnManagerPropertyChangeRegistration(const string& interface,
                                                      const string& signal_name,
                                                      bool success) {
  VLOG(3) << "Registered ManagerPropertyChange handler.";
  CHECK(success) << "privetd requires Manager signals.";
  VariantDictionary properties;
  if (!manager_proxy_.GetProperties(&properties, nullptr)) {
    LOG(ERROR) << "Unable to get properties from Manager, waiting for "
                  "Manager to come back online.";
    return;
  }
  auto it = properties.find(shill::kDevicesProperty);
  CHECK(it != properties.end()) << "Shill should always publish a device list.";
  OnManagerPropertyChange(shill::kDevicesProperty, it->second);
}

void ShillClient::OnManagerPropertyChange(const string& property_name,
                                          const Any& property_value) {
  if (property_name != shill::kDevicesProperty) {
    return;
  }
  VLOG(3) << "Manager's device list has changed.";
  // We're going to remove every device we haven't seen in the update.
  set<ObjectPath> device_paths_to_remove;
  for (const auto& kv : devices_) {
    device_paths_to_remove.insert(kv.first);
  }
  for (const auto& device_path : property_value.TryGet<vector<ObjectPath>>()) {
    if (!device_path.IsValid()) {
      LOG(ERROR) << "Ignoring invalid device path in Manager's device list.";
      return;
    }
    auto it = devices_.find(device_path);
    if (it != devices_.end()) {
      // Found an existing proxy.  Since the whitelist never changes,
      // this still a valid device.
      device_paths_to_remove.erase(device_path);
      continue;
    }
    std::unique_ptr<DeviceProxy> device{new DeviceProxy{bus_, device_path}};
    if (!IsMonitoredDevice(device.get())) {
      continue;
    }
    device->RegisterPropertyChangedSignalHandler(
        base::Bind(&ShillClient::OnDevicePropertyChange,
                   weak_factory_.GetWeakPtr(), device_path),
        base::Bind(&ShillClient::OnDevicePropertyChangeRegistration,
                   weak_factory_.GetWeakPtr(), device_path));
    VLOG(3) << "Creating device proxy at " << device_path.value();
    devices_[device_path].device = std::move(device);
  }
  // Clean up devices/services related to removed devices.
  if (!device_paths_to_remove.empty()) {
    for (const ObjectPath& device_path : device_paths_to_remove) {
      devices_.erase(device_path);
    }
    UpdateConnectivityState();
  }
}

void ShillClient::OnDevicePropertyChangeRegistration(
    const ObjectPath& device_path,
    const string& interface,
    const string& signal_name,
    bool success) {
  VLOG(3) << "Registered DevicePropertyChange handler.";
  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    return;
  }
  CHECK(success) << "Failed to subscribe to Device property changes.";
  DeviceProxy* device = it->second.device.get();
  VariantDictionary properties;
  if (!device->GetProperties(&properties, nullptr)) {
    LOG(WARNING) << "Failed to get device properties?";
    return;
  }
  auto prop_it = properties.find(shill::kSelectedServiceProperty);
  if (prop_it == properties.end()) {
    LOG(WARNING) << "Failed to get device's selected service?";
    return;
  }
  OnDevicePropertyChange(device_path, shill::kSelectedServiceProperty,
                         prop_it->second);
}

void ShillClient::OnDevicePropertyChange(const ObjectPath& device_path,
                                         const string& property_name,
                                         const Any& property_value) {
  // We only care about selected services anyway.
  if (property_name != shill::kSelectedServiceProperty) {
    return;
  }
  // If the device isn't our list of whitelisted devices, ignore it.
  auto it = devices_.find(device_path);
  if (it == devices_.end()) {
    return;
  }
  DeviceState& device_state = it->second;
  ObjectPath service_path{property_value.TryGet<ObjectPath>()};
  if (!service_path.IsValid()) {
    LOG(ERROR) << "Device at " << device_path.value()
               << " selected invalid service path.";
    return;
  }
  VLOG(3) << "Device at " << it->first.value() << " has selected service at "
          << service_path.value();
  bool removed_old_service{false};
  if (device_state.selected_service) {
    if (device_state.selected_service->GetObjectPath() == service_path) {
      return;  // Spurious update?
    }
    device_state.selected_service.reset();
    device_state.service_state = ServiceState::kOffline;
    removed_old_service = true;
  }
  std::shared_ptr<ServiceProxy> new_service;
  const bool reuse_connecting_service =
      service_path.value() != "/" && connecting_service_ &&
      connecting_service_->GetObjectPath() == service_path;
  if (reuse_connecting_service) {
    new_service = connecting_service_;
    // When we reuse the connecting service, we need to make sure that our
    // cached state is correct.  Normally, we do this by relying reading the
    // state when our signal handlers finish registering, but this may have
    // happened long in the past for the connecting service.
    string state;
    if (GetStateForService(new_service.get(), &state)) {
      device_state.service_state = ShillServiceStateToServiceState(state);
    } else {
      LOG(WARNING) << "Failed to read properties from existing service "
                      "on selection.";
    }
  } else if (service_path.value() != "/") {
    // The device has selected a new service we haven't see before.
    new_service.reset(new ServiceProxy{bus_, service_path});
    new_service->RegisterPropertyChangedSignalHandler(
        base::Bind(&ShillClient::OnServicePropertyChange,
                   weak_factory_.GetWeakPtr(), service_path),
        base::Bind(&ShillClient::OnServicePropertyChangeRegistration,
                   weak_factory_.GetWeakPtr(), service_path));
  }
  device_state.selected_service = new_service;
  if (reuse_connecting_service || removed_old_service) {
    UpdateConnectivityState();
  }
}

void ShillClient::OnServicePropertyChangeRegistration(const ObjectPath& path,
                                                      const string& interface,
                                                      const string& signal_name,
                                                      bool success) {
  VLOG(3) << "OnServicePropertyChangeRegistration(" << path.value() << ");";
  ServiceProxy* service{nullptr};
  if (connecting_service_ && connecting_service_->GetObjectPath() == path) {
    // Note that the connecting service might also be a selected service.
    service = connecting_service_.get();
    if (!success) {
      CleanupConnectingService(false);
    }
  } else {
    for (const auto& kv : devices_) {
      if (kv.second.selected_service &&
          kv.second.selected_service->GetObjectPath() == path) {
        service = kv.second.selected_service.get();
        break;
      }
    }
  }
  if (service == nullptr || !success) {
    return;  // A failure or success for a proxy we no longer care about.
  }
  VariantDictionary properties;
  if (!service->GetProperties(&properties, nullptr)) {
    return;
  }
  // Give ourselves property changed signals for the initial property
  // values.
  auto it = properties.find(shill::kStateProperty);
  if (it != properties.end()) {
    OnServicePropertyChange(path, shill::kStateProperty, it->second);
  }
  it = properties.find(shill::kSignalStrengthProperty);
  if (it != properties.end()) {
    OnServicePropertyChange(path, shill::kSignalStrengthProperty, it->second);
  }
}

void ShillClient::OnServicePropertyChange(const ObjectPath& service_path,
                                          const string& property_name,
                                          const Any& property_value) {
  VLOG(3) << "ServicePropertyChange(" << service_path.value() << ", "
          << property_name << ", ...);";
  if (property_name == shill::kStateProperty) {
    const string state{property_value.TryGet<string>()};
    if (state.empty()) {
      VLOG(3) << "Invalid service state update.";
      return;
    }
    VLOG(3) << "New service state=" << state;
    OnStateChangeForSelectedService(service_path, state);
    OnStateChangeForConnectingService(service_path, state);
  } else if (property_name == shill::kSignalStrengthProperty) {
    OnStrengthChangeForConnectingService(service_path,
                                         property_value.TryGet<uint8_t>());
  }
}

void ShillClient::OnStateChangeForConnectingService(
    const ObjectPath& service_path,
    const string& state) {
  if (!connecting_service_ ||
      connecting_service_->GetObjectPath() != service_path ||
      ShillServiceStateToServiceState(state) != ServiceState::kConnected) {
    return;
  }
  connecting_service_reset_pending_ = true;
  on_connect_success_.callback().Run();
  CleanupConnectingService(true);
}

void ShillClient::OnStrengthChangeForConnectingService(
    const ObjectPath& service_path,
    uint8_t signal_strength) {
  if (!connecting_service_ ||
      connecting_service_->GetObjectPath() != service_path ||
      signal_strength <= 0 || have_called_connect_) {
    return;
  }
  VLOG(1) << "Connecting service has signal. Calling Connect().";
  have_called_connect_ = true;
  // Failures here indicate that we've already connected,
  // or are connecting, or some other very unexciting thing.
  // Ignore all that, and rely on state changes to detect
  // connectivity.
  connecting_service_->Connect(nullptr);
}

void ShillClient::OnStateChangeForSelectedService(
    const ObjectPath& service_path,
    const string& state) {
  // Find the device/service pair responsible for this update
  VLOG(3) << "State for potentially selected service " << service_path.value()
          << " have changed to " << state;
  for (auto& kv : devices_) {
    if (kv.second.selected_service &&
        kv.second.selected_service->GetObjectPath() == service_path) {
      VLOG(3) << "Updated cached connection state for selected service.";
      kv.second.service_state = ShillServiceStateToServiceState(state);
      UpdateConnectivityState();
      return;
    }
  }
}

void ShillClient::UpdateConnectivityState() {
  // Update the connectivity state of the device by picking the
  // state of the currently most connected selected service.
  ServiceState new_connectivity_state{ServiceState::kOffline};
  for (const auto& kv : devices_) {
    if (kv.second.service_state > new_connectivity_state) {
      new_connectivity_state = kv.second.service_state;
    }
  }
  VLOG(1) << "Connectivity changed: "
          << ServiceStateToString(connectivity_state_) << " -> "
          << ServiceStateToString(new_connectivity_state);
  // Notify listeners even if state changed to the same value. Listeners may
  // want to handle this event.
  connectivity_state_ = new_connectivity_state;
  // We may call UpdateConnectivityState whenever we mutate a data structure
  // such that our connectivity status could change.  However, we don't want
  // to allow people to call into ShillClient while some other operation is
  // underway.  Therefore, call our callbacks later, when we're in a good
  // state.
  base::MessageLoop::current()->PostTask(
      FROM_HERE, base::Bind(&ShillClient::NotifyConnectivityListeners,
                            weak_factory_.GetWeakPtr(), AmOnline()));
}

void ShillClient::NotifyConnectivityListeners(bool am_online) {
  VLOG(3) << "Notifying connectivity listeners that online=" << am_online;
  for (const auto& listener : connectivity_listeners_) {
    listener.Run(am_online);
  }
}

void ShillClient::CleanupConnectingService(bool check_for_reset_pending) {
  if (check_for_reset_pending && !connecting_service_reset_pending_) {
    return;  // Must have called connect before we got here.
  }
  if (connecting_service_) {
    connecting_service_->ReleaseObjectProxy(base::Bind(&IgnoreDetachEvent));
    connecting_service_.reset();
  }
  on_connect_success_.Cancel();
  have_called_connect_ = false;
  connecting_service_reset_pending_ = false;
}

}  // namespace privetd
