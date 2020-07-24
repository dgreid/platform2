// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/device_dbus_adaptor.h"

#include <utility>

#include "shill/device.h"
#include "shill/error.h"
#include "shill/logging.h"

using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const DeviceDBusAdaptor* d) {
  return d->GetRpcIdentifier().value() + " (" + d->device()->UniqueName() + ")";
}
}  // namespace Logging

// static
const char DeviceDBusAdaptor::kPath[] = "/device/";

DeviceDBusAdaptor::DeviceDBusAdaptor(const scoped_refptr<dbus::Bus>& bus,
                                     Device* device)
    : org::chromium::flimflam::DeviceAdaptor(this),
      DBusAdaptor(bus, kPath + SanitizePathElement(device->UniqueName())),
      device_(device) {
  // Register DBus object.
  RegisterWithDBusObject(dbus_object());
  dbus_object()->RegisterAndBlock();
}

DeviceDBusAdaptor::~DeviceDBusAdaptor() {
  dbus_object()->UnregisterAsync();
  device_ = nullptr;
}

const RpcIdentifier& DeviceDBusAdaptor::GetRpcIdentifier() const {
  return dbus_path();
}

void DeviceDBusAdaptor::EmitBoolChanged(const string& name, bool value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitUintChanged(const string& name, uint32_t value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitUint16Changed(const string& name, uint16_t value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitIntChanged(const string& name, int value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitStringChanged(const string& name,
                                          const string& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitStringmapChanged(const string& name,
                                             const Stringmap& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitStringmapsChanged(const string& name,
                                              const Stringmaps& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitStringsChanged(const string& name,
                                           const Strings& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitKeyValueStoreChanged(const string& name,
                                                 const KeyValueStore& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  brillo::VariantDictionary dict =
      KeyValueStore::ConvertToVariantDictionary(value);
  SendPropertyChangedSignal(name, brillo::Any(dict));
}

void DeviceDBusAdaptor::EmitRpcIdentifierChanged(const std::string& name,
                                                 const RpcIdentifier& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void DeviceDBusAdaptor::EmitRpcIdentifierArrayChanged(
    const string& name, const RpcIdentifiers& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  vector<dbus::ObjectPath> paths;
  for (const auto& element : value) {
    paths.push_back(dbus::ObjectPath(element));
  }

  SendPropertyChangedSignal(name, brillo::Any(paths));
}

bool DeviceDBusAdaptor::GetProperties(
    brillo::ErrorPtr* error, brillo::VariantDictionary* out_properties) {
  SLOG(this, 2) << __func__;
  return DBusAdaptor::GetProperties(device_->store(), out_properties, error);
}

bool DeviceDBusAdaptor::SetProperty(brillo::ErrorPtr* error,
                                    const string& name,
                                    const brillo::Any& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  return DBusAdaptor::SetProperty(device_->mutable_store(), name, value, error);
}

bool DeviceDBusAdaptor::ClearProperty(brillo::ErrorPtr* error,
                                      const string& name) {
  SLOG(this, 2) << __func__ << ": " << name;
  return DBusAdaptor::ClearProperty(device_->mutable_store(), name, error);
}

void DeviceDBusAdaptor::Enable(DBusMethodResponsePtr<> response) {
  SLOG(this, 2) << __func__;
  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->SetEnabledPersistent(true, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::Disable(DBusMethodResponsePtr<> response) {
  SLOG(this, 2) << __func__ << ": Device " << device_->UniqueName();
  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->SetEnabledPersistent(false, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::Register(DBusMethodResponsePtr<> response,
                                 const string& network_id) {
  SLOG(this, 2) << __func__ << ": " << network_id;
  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->RegisterOnNetwork(network_id, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::RequirePin(DBusMethodResponsePtr<> response,
                                   const string& pin,
                                   bool require) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->RequirePin(pin, require, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::EnterPin(DBusMethodResponsePtr<> response,
                                 const string& pin) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->EnterPin(pin, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::UnblockPin(DBusMethodResponsePtr<> response,
                                   const string& unblock_code,
                                   const string& pin) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->UnblockPin(unblock_code, pin, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::ChangePin(DBusMethodResponsePtr<> response,
                                  const string& old_pin,
                                  const string& new_pin) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->ChangePin(old_pin, new_pin, &e, callback);
  ReturnResultOrDefer(callback, e);
}

void DeviceDBusAdaptor::Reset(DBusMethodResponsePtr<> response) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->Reset(&e, callback);
  ReturnResultOrDefer(callback, e);
}

bool DeviceDBusAdaptor::RenewDHCPLease(brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;
  Error e;
  device_->RenewDHCPLease(true, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::PerformTDLSOperation(brillo::ErrorPtr* error,
                                             const string& operation,
                                             const string& peer,
                                             string* out_state) {
  SLOG(this, 2) << __func__;

  Error e;
  Error::PopulateAndLog(FROM_HERE, &e, Error::kNotSupported,
                        "PerformTDLSOperation is deprecated");
  return false;
}

bool DeviceDBusAdaptor::ResetByteCounters(brillo::ErrorPtr* error) {
  device_->ResetByteCounters();
  return true;
}

bool DeviceDBusAdaptor::RequestRoam(brillo::ErrorPtr* error,
                                    const std::string& addr) {
  SLOG(this, 2) << __func__ << ": " << addr;
  Error e;
  device_->RequestRoam(addr, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::AddWakeOnPacketConnection(brillo::ErrorPtr* error,
                                                  const string& ip_endpoint) {
  SLOG(this, 2) << __func__;

  Error e;
  device_->AddWakeOnPacketConnection(ip_endpoint, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::AddWakeOnPacketOfTypes(
    brillo::ErrorPtr* error, const std::vector<std::string>& packet_types) {
  SLOG(this, 2) << __func__;

  Error e;
  device_->AddWakeOnPacketOfTypes(packet_types, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::RemoveWakeOnPacketConnection(
    brillo::ErrorPtr* error, const string& ip_endpoint) {
  SLOG(this, 2) << __func__;

  Error e;
  device_->RemoveWakeOnPacketConnection(ip_endpoint, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::RemoveWakeOnPacketOfTypes(
    brillo::ErrorPtr* error, const std::vector<std::string>& packet_types) {
  SLOG(this, 2) << __func__;

  Error e;
  device_->RemoveWakeOnPacketOfTypes(packet_types, &e);
  return !e.ToChromeosError(error);
}

bool DeviceDBusAdaptor::RemoveAllWakeOnPacketConnections(
    brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;

  Error e;
  device_->RemoveAllWakeOnPacketConnections(&e);
  return !e.ToChromeosError(error);
}

void DeviceDBusAdaptor::SetUsbEthernetMacAddressSource(
    DBusMethodResponsePtr<> response, const std::string& source) {
  SLOG(this, 2) << __func__;

  Error e(Error::kOperationInitiated);
  ResultCallback callback = GetMethodReplyCallback(std::move(response));
  device_->SetUsbEthernetMacAddressSource(source, &e, callback);
  ReturnResultOrDefer(callback, e);
}

}  // namespace shill
