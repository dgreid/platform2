// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_TRACKER_H_
#define MODEMFWD_MODEM_TRACKER_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_path.h>
#include <shill/dbus-proxies.h>

namespace modemfwd {

using OnModemAppearedCallback =
    base::Callback<void(std::unique_ptr<org::chromium::flimflam::DeviceProxy>)>;

class ModemTracker {
 public:
  ModemTracker(scoped_refptr<dbus::Bus> bus,
               const OnModemAppearedCallback& on_modem_appeared_callback);
  ModemTracker(const ModemTracker&) = delete;
  ModemTracker& operator=(const ModemTracker&) = delete;

  ~ModemTracker() = default;

 private:
  // Called when shill appears or disappears.
  void OnServiceAvailable(bool available);

  // Called when a property on the shill manager changes.
  void OnPropertyChanged(const std::string& property_name,
                         const brillo::Any& property_value);

  // Called when the device list changes.
  void OnDeviceListChanged(const std::vector<dbus::ObjectPath>& new_list);

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> shill_proxy_;
  OnModemAppearedCallback on_modem_appeared_callback_;

  std::set<dbus::ObjectPath> modem_objects_;

  base::WeakPtrFactory<ModemTracker> weak_ptr_factory_;
};

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_TRACKER_H_
