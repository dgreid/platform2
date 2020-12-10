// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/dbus_properties_proxy.h"

#include <utility>

#include <base/memory/ptr_util.h>

#include "shill/dbus/fake_properties_proxy.h"
#include "shill/logging.h"

namespace shill {

using std::string;
using std::vector;

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

namespace {

void RunSuccessCallback(
    const base::Callback<void(const KeyValueStore&)>& success_callback,
    const brillo::VariantDictionary& properties) {
  success_callback.Run(KeyValueStore::ConvertFromVariantDictionary(properties));
}

void RunErrorCallback(const base::Callback<void(const Error&)>& error_callback,
                      brillo::Error* dbus_error) {
  error_callback.Run(Error(Error::kOperationFailed, dbus_error->GetMessage()));
}

}  // namespace

DBusPropertiesProxy::DBusPropertiesProxy(const scoped_refptr<dbus::Bus>& bus,
                                         const RpcIdentifier& path,
                                         const string& service)
    : proxy_(new org::freedesktop::DBus::PropertiesProxy(
          bus, service, dbus::ObjectPath(path))) {
  // Register signal handlers.
  proxy_->RegisterPropertiesChangedSignalHandler(
      base::Bind(&DBusPropertiesProxy::PropertiesChanged,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&DBusPropertiesProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));
  proxy_->RegisterMmPropertiesChangedSignalHandler(
      base::Bind(&DBusPropertiesProxy::MmPropertiesChanged,
                 weak_factory_.GetWeakPtr()),
      base::Bind(&DBusPropertiesProxy::OnSignalConnected,
                 weak_factory_.GetWeakPtr()));
}

DBusPropertiesProxy::~DBusPropertiesProxy() = default;

// Test only private constructor.
DBusPropertiesProxy::DBusPropertiesProxy(
    std::unique_ptr<org::freedesktop::DBus::PropertiesProxyInterface> proxy)
    : proxy_(std::move(proxy)) {}

// static
std::unique_ptr<DBusPropertiesProxy>
DBusPropertiesProxy::CreateDBusPropertiesProxyForTesting() {
  // Use WrapUnique to allow test constructor to be private.
  return base::WrapUnique(
      new DBusPropertiesProxy(std::make_unique<FakePropertiesProxy>()));
}

FakePropertiesProxy* DBusPropertiesProxy::GetFakePropertiesProxyForTesting() {
  return static_cast<FakePropertiesProxy*>(proxy_.get());
}

KeyValueStore DBusPropertiesProxy::GetAll(const string& interface_name) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << "(" << interface_name << ")";
  brillo::VariantDictionary properties_dict;
  brillo::ErrorPtr error;
  if (!proxy_->GetAll(interface_name, &properties_dict, &error)) {
    LOG(ERROR) << __func__ << " failed on " << interface_name << ": "
               << error->GetCode() << " " << error->GetMessage();
    return KeyValueStore();
  }
  KeyValueStore properties_store =
      KeyValueStore::ConvertFromVariantDictionary(properties_dict);
  return properties_store;
}

void DBusPropertiesProxy::GetAllAsync(
    const std::string& interface_name,
    const base::Callback<void(const KeyValueStore&)>& success_callback,
    const base::Callback<void(const Error&)>& error_callback) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << "(" << interface_name << ")";
  proxy_->GetAllAsync(interface_name,
                      base::Bind(RunSuccessCallback, success_callback),
                      base::Bind(RunErrorCallback, error_callback));
}

brillo::Any DBusPropertiesProxy::Get(const string& interface_name,
                                     const string& property) {
  SLOG(&proxy_->GetObjectPath(), 2)
      << __func__ << "(" << interface_name << ", " << property << ")";
  brillo::Any value;
  brillo::ErrorPtr error;
  if (!proxy_->Get(interface_name, property, &value, &error)) {
    LOG(ERROR) << __func__ << " failed for " << interface_name << " "
               << property << ": " << error->GetCode() << " "
               << error->GetMessage();
  }
  return value;
}

void DBusPropertiesProxy::GetAsync(
    const std::string& interface_name,
    const std::string& property,
    const base::Callback<void(const brillo::Any&)>& success_callback,
    const base::Callback<void(const Error&)>& error_callback) {
  SLOG(&proxy_->GetObjectPath(), 2)
      << __func__ << "(" << interface_name << ", " << property << ")";
  proxy_->GetAsync(interface_name, property, success_callback,
                   base::Bind(RunErrorCallback, error_callback));
}

void DBusPropertiesProxy::MmPropertiesChanged(
    const string& interface, const brillo::VariantDictionary& properties) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << "(" << interface << ")";
  KeyValueStore properties_store =
      KeyValueStore::ConvertFromVariantDictionary(properties);
  mm_properties_changed_callback_.Run(interface, properties_store);
}

void DBusPropertiesProxy::PropertiesChanged(
    const string& interface,
    const brillo::VariantDictionary& changed_properties,
    const vector<string>& /*invalidated_properties*/) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << "(" << interface << ")";
  KeyValueStore properties_store =
      KeyValueStore::ConvertFromVariantDictionary(changed_properties);
  properties_changed_callback_.Run(interface, properties_store);
}

void DBusPropertiesProxy::OnSignalConnected(const string& interface_name,
                                            const string& signal_name,
                                            bool success) {
  SLOG(&proxy_->GetObjectPath(), 2)
      << __func__ << "interface: " << interface_name
      << " signal: " << signal_name << "success: " << success;
  if (!success) {
    LOG(ERROR) << "Failed to connect signal " << signal_name << " to interface "
               << interface_name;
  }
}

}  // namespace shill
