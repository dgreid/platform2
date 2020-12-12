// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_
#define SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <brillo/any.h>

#include "cellular/dbus-proxies.h"
#include "shill/error.h"
#include "shill/key_value_store.h"

namespace shill {

class FakePropertiesProxy;

// This is a cellular-specific DBus Properties interface, as it supports
// cellular-specific signal (ModemManagerPropertiesChanged).
// These are the methods that a DBusProperties proxy must support.
class DBusPropertiesProxy {
 public:
  // Callback invoked when an object sends a DBus property change signal.
  using PropertiesChangedCallback = base::Callback<void(
      const std::string& interface, const KeyValueStore& changed_properties)>;

  // Callback invoked when the classic modem manager sends a DBus
  // property change signal.
  using ModemManagerPropertiesChangedCallback = base::Callback<void(
      const std::string& interface, const KeyValueStore& properties)>;

  DBusPropertiesProxy(const scoped_refptr<dbus::Bus>& bus,
                      const RpcIdentifier& path,
                      const std::string& service);
  DBusPropertiesProxy(const DBusPropertiesProxy&) = delete;
  DBusPropertiesProxy& operator=(const DBusPropertiesProxy&) = delete;

  ~DBusPropertiesProxy();

  KeyValueStore GetAll(const std::string& interface_name);
  void GetAllAsync(
      const std::string& interface_name,
      const base::Callback<void(const KeyValueStore&)>& success_callback,
      const base::Callback<void(const Error&)>& error_callback);
  brillo::Any Get(const std::string& interface_name,
                  const std::string& property);
  void GetAsync(
      const std::string& interface_name,
      const std::string& property,
      const base::Callback<void(const brillo::Any&)>& success_callback,
      const base::Callback<void(const Error&)>& error_callback);

  void SetPropertiesChangedCallback(const PropertiesChangedCallback& callback);

  void SetModemManagerPropertiesChangedCallback(
      const ModemManagerPropertiesChangedCallback& callback);

  static std::unique_ptr<DBusPropertiesProxy>
  CreateDBusPropertiesProxyForTesting();

  // Only use this with CreateDBusPropertiesProxyForTesting().
  FakePropertiesProxy* GetFakePropertiesProxyForTesting();

 private:
  // Test only constructor.
  explicit DBusPropertiesProxy(
      std::unique_ptr<org::freedesktop::DBus::PropertiesProxyInterface> proxy);

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

  std::unique_ptr<org::freedesktop::DBus::PropertiesProxyInterface> proxy_;

  base::WeakPtrFactory<DBusPropertiesProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_DBUS_PROPERTIES_PROXY_H_
