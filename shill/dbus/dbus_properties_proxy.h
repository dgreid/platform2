// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_
#define SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>

#include "cellular/dbus-proxies.h"
#include "shill/dbus_properties_proxy_interface.h"

namespace shill {

// DBusPropertiesProxyInterface is a cellular-specific interface, refer to its
// header for more info.
class DBusPropertiesProxy : public DBusPropertiesProxyInterface {
 public:
  DBusPropertiesProxy(const scoped_refptr<dbus::Bus>& bus,
                      const RpcIdentifier& path,
                      const std::string& service);
  DBusPropertiesProxy(const DBusPropertiesProxy&) = delete;
  DBusPropertiesProxy& operator=(const DBusPropertiesProxy&) = delete;

  ~DBusPropertiesProxy() override;

  // Inherited from DBusPropertiesProxyInterface.
  KeyValueStore GetAll(const std::string& interface_name) override;
  void GetAllAsync(
      const std::string& interface_name,
      const base::Callback<void(const KeyValueStore&)>& success_callback,
      const base::Callback<void(const Error&)>& error_callback) override;
  brillo::Any Get(const std::string& interface_name,
                  const std::string& property) override;
  void GetAsync(
      const std::string& interface_name,
      const std::string& property,
      const base::Callback<void(const brillo::Any&)>& success_callback,
      const base::Callback<void(const Error&)>& error_callback) override;

  void set_properties_changed_callback(
      const PropertiesChangedCallback& callback) override {
    properties_changed_callback_ = callback;
  }

  void set_modem_manager_properties_changed_callback(
      const ModemManagerPropertiesChangedCallback& callback) override {
    mm_properties_changed_callback_ = callback;
  }

 private:
  // Signal handlers.
  void MmPropertiesChanged(const std::string& interface,
                           const brillo::VariantDictionary& properties);
  void PropertiesChanged(
      const std::string& interface,
      const brillo::VariantDictionary& changed_properties,
      const std::vector<std::string>& invalidated_properties);

  // Called when signal is connected to the ObjectProxy.
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  PropertiesChangedCallback properties_changed_callback_;
  ModemManagerPropertiesChangedCallback mm_properties_changed_callback_;

  std::unique_ptr<org::freedesktop::DBus::PropertiesProxy> proxy_;

  base::WeakPtrFactory<DBusPropertiesProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_
