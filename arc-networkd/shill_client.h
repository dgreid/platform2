// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORKD_SHILL_CLIENT_H_
#define ARC_NETWORKD_SHILL_CLIENT_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <shill/dbus-proxies.h>

namespace arc_networkd {

// Listens for shill signals over dbus in order to figure out which
// network interface (if any) is being used as the default service.
class ShillClient {
 public:
  explicit ShillClient(scoped_refptr<dbus::Bus> bus);
  virtual ~ShillClient() {}

  void RegisterDefaultInterfaceChangedHandler(
      const base::Callback<void(const std::string&)>& callback);

 protected:
  void OnManagerPropertyChangeRegistration(const std::string& interface,
                                           const std::string& signal_name,
                                           bool success);
  void OnManagerPropertyChange(const std::string& property_name,
                               const brillo::Any& property_value);

  bool GetDefaultInterface(std::string* name);

  std::string default_interface_;
  base::Callback<void(const std::string&)> default_interface_callback_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::flimflam::ManagerProxy> manager_proxy_;

  base::WeakPtrFactory<ShillClient> weak_factory_{this};

 private:
  DISALLOW_COPY_AND_ASSIGN(ShillClient);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORKD_SHILL_CLIENT_H_
