// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/logging.h"
#include "modemfwd/modem_tracker.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>

namespace modemfwd {

namespace {

void OnSignalConnected(const std::string& interface_name,
                       const std::string& signal_name,
                       bool success) {
  DVLOG(1) << (success ? "Connected" : "Failed to connect") << " to signal "
           << signal_name << " of " << interface_name;
}

}  // namespace

ModemTracker::ModemTracker(
    scoped_refptr<dbus::Bus> bus,
    const OnModemAppearedCallback& on_modem_appeared_callback)
    : bus_(bus),
      shill_proxy_(new org::chromium::flimflam::ManagerProxy(bus)),
      on_modem_appeared_callback_(on_modem_appeared_callback),
      weak_ptr_factory_(this) {
  shill_proxy_->GetObjectProxy()->WaitForServiceToBeAvailable(base::Bind(
      &ModemTracker::OnServiceAvailable, weak_ptr_factory_.GetWeakPtr()));
}

void ModemTracker::OnServiceAvailable(bool available) {
  if (!available) {
    LOG(WARNING) << "shill disappeared";
    modem_objects_.clear();
    return;
  }

  shill_proxy_->RegisterPropertyChangedSignalHandler(
      base::Bind(&ModemTracker::OnManagerPropertyChanged,
                 weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&OnSignalConnected));

  brillo::ErrorPtr error;
  brillo::VariantDictionary properties;
  if (!shill_proxy_->GetProperties(&properties, &error)) {
    LOG(ERROR) << "Could not get property list from shill: "
               << error->GetMessage();
    return;
  }

  OnDeviceListChanged(properties[shill::kDevicesProperty]
                          .TryGet<std::vector<dbus::ObjectPath>>());
}

void ModemTracker::OnManagerPropertyChanged(const std::string& property_name,
                                            const brillo::Any& property_value) {
  if (property_name == shill::kDevicesProperty)
    OnDeviceListChanged(property_value.TryGet<std::vector<dbus::ObjectPath>>());
}

void ModemTracker::OnDevicePropertyChanged(dbus::ObjectPath device_path,
                                           const std::string& property_name,
                                           const brillo::Any& property_value) {
  // Listen only for the ICCID change triggered by a SIM change
  if (property_name != shill::kIccidProperty)
    return;

  auto current_iccid = modem_objects_.find(device_path);
  if (current_iccid == modem_objects_.end())
    return;

  std::string iccid(property_value.TryGet<std::string>());
  if (iccid == current_iccid->second)
    return;

  current_iccid->second = iccid;

  ELOG(INFO) << "SIM ICCID changed to [" << iccid.substr(0, 9)
             << "...] for device " << device_path.value();

  // SIM removed, wait for a real one to update
  if (iccid.empty())
    return;

  // trigger the firmware update
  auto device =
      std::make_unique<org::chromium::flimflam::DeviceProxy>(bus_, device_path);
  on_modem_appeared_callback_.Run(std::move(device));
}

void ModemTracker::OnDeviceListChanged(
    const std::vector<dbus::ObjectPath>& new_list) {
  std::map<dbus::ObjectPath, std::string> new_modems;
  for (const auto& device_path : new_list) {
    if (modem_objects_.find(device_path) != modem_objects_.end()) {
      // Keep existing devices in the new list.
      new_modems[device_path] = modem_objects_[device_path];
      continue;
    }

    // See if the modem object is of cellular type.
    auto device = std::make_unique<org::chromium::flimflam::DeviceProxy>(
        bus_, device_path);
    brillo::ErrorPtr error;
    brillo::VariantDictionary properties;
    if (!device->GetProperties(&properties, &error)) {
      LOG(ERROR) << "Could not get property list for device "
                 << device_path.value() << ": " << error->GetMessage();
      continue;
    }

    if (properties[shill::kTypeProperty].TryGet<std::string>() !=
        shill::kTypeCellular) {
      DVLOG(1) << "Device " << device_path.value()
               << " is not cellular type, ignoring";
      continue;
    }

    // Record the modem device with its current ICCID
    new_modems[device_path] =
        properties[shill::kIccidProperty].TryGet<std::string>();

    // Listen to the Device ICCID property in order to detect future SIM swaps.
    device->RegisterPropertyChangedSignalHandler(
        base::Bind(&ModemTracker::OnDevicePropertyChanged,
                   weak_ptr_factory_.GetWeakPtr(), device_path),
        base::Bind(&OnSignalConnected));

    on_modem_appeared_callback_.Run(std::move(device));
  }
  modem_objects_ = new_modems;
}

}  // namespace modemfwd
