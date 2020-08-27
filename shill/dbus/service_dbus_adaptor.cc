// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/service_dbus_adaptor.h"

#include <map>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>

#include "shill/error.h"
#include "shill/logging.h"
#include "shill/service.h"

using std::map;
using std::string;
using std::vector;

namespace {
const char kDBusRpcReasonString[] = "D-Bus RPC";
}  // namespace

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const ServiceDBusAdaptor* s) {
  return s->GetRpcIdentifier().value() + " (" + s->service()->log_name() + ")";
}
}  // namespace Logging

// static
const char ServiceDBusAdaptor::kPath[] = "/service/";

ServiceDBusAdaptor::ServiceDBusAdaptor(const scoped_refptr<dbus::Bus>& bus,
                                       Service* service)
    : org::chromium::flimflam::ServiceAdaptor(this),
      DBusAdaptor(bus, kPath + service->GetDBusObjectPathIdentifer()),
      service_(service) {
  // Register DBus object.
  RegisterWithDBusObject(dbus_object());
  dbus_object()->RegisterAndBlock();
}

ServiceDBusAdaptor::~ServiceDBusAdaptor() {
  dbus_object()->UnregisterAsync();
  service_ = nullptr;
}

void ServiceDBusAdaptor::EmitBoolChanged(const string& name, bool value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitUint8Changed(const string& name, uint8_t value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitUint16Changed(const string& name, uint16_t value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitUint16sChanged(const string& name,
                                            const Uint16s& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitUintChanged(const string& name, uint32_t value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitIntChanged(const string& name, int value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitRpcIdentifierChanged(const string& name,
                                                  const RpcIdentifier& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitStringChanged(const string& name,
                                           const string& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

void ServiceDBusAdaptor::EmitStringmapChanged(const string& name,
                                              const Stringmap& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  SendPropertyChangedSignal(name, brillo::Any(value));
}

bool ServiceDBusAdaptor::GetProperties(brillo::ErrorPtr* error,
                                       brillo::VariantDictionary* properties) {
  SLOG(this, 2) << __func__;
  return DBusAdaptor::GetProperties(service_->store(), properties, error);
}

bool ServiceDBusAdaptor::SetProperty(brillo::ErrorPtr* error,
                                     const string& name,
                                     const brillo::Any& value) {
  SLOG(this, 2) << __func__ << ": " << name;
  return DBusAdaptor::SetProperty(service_->mutable_store(), name, value,
                                  error);
}

bool ServiceDBusAdaptor::SetProperties(brillo::ErrorPtr* error,
                                       const brillo::VariantDictionary& args) {
  SLOG(this, 2) << __func__;
  KeyValueStore args_store = KeyValueStore::ConvertFromVariantDictionary(args);
  Error configure_error;
  service_->Configure(args_store, &configure_error);
  return !configure_error.ToChromeosError(error);
}

bool ServiceDBusAdaptor::ClearProperty(brillo::ErrorPtr* error,
                                       const string& name) {
  SLOG(this, 2) << __func__ << ": " << name;
  bool status =
      DBusAdaptor::ClearProperty(service_->mutable_store(), name, error);
  if (status) {
    service_->OnPropertyChanged(name);
  }
  return status;
}

bool ServiceDBusAdaptor::ClearProperties(brillo::ErrorPtr* /*error*/,
                                         const vector<string>& names,
                                         vector<bool>* results) {
  SLOG(this, 2) << __func__;
  for (const auto& name : names) {
    results->push_back(ClearProperty(nullptr, name));
  }
  return true;
}

bool ServiceDBusAdaptor::Connect(brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;
  Error e;
  service_->UserInitiatedConnect(kDBusRpcReasonString, &e);
  return !e.ToChromeosError(error);
}

bool ServiceDBusAdaptor::Disconnect(brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;
  Error e;
  service_->UserInitiatedDisconnect(kDBusRpcReasonString, &e);
  return !e.ToChromeosError(error);
}

bool ServiceDBusAdaptor::Remove(brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;
  Error e;
  service_->Remove(&e);
  return !e.ToChromeosError(error);
}

bool ServiceDBusAdaptor::ActivateCellularModem(brillo::ErrorPtr* error,
                                               const string& carrier) {
  SLOG(this, 2) << __func__;
  Error e;
  Error::PopulateAndLog(FROM_HERE, &e, Error::kNotSupported,
                        "Service doesn't support cellular modem activation.");
  return !e.ToChromeosError(error);
}

bool ServiceDBusAdaptor::CompleteCellularActivation(brillo::ErrorPtr* error) {
  SLOG(this, 2) << __func__;
  Error e;
  service_->CompleteCellularActivation(&e);
  return !e.ToChromeosError(error);
}

bool ServiceDBusAdaptor::GetLoadableProfileEntries(
    brillo::ErrorPtr* /*error*/, map<dbus::ObjectPath, string>* entries) {
  SLOG(this, 2) << __func__;
  map<RpcIdentifier, string> profile_entry_strings =
      service_->GetLoadableProfileEntries();
  for (const auto& entry : profile_entry_strings) {
    (*entries)[dbus::ObjectPath(entry.first)] = entry.second;
  }
  return true;
}

bool ServiceDBusAdaptor::GetWiFiPassphrase(brillo::ErrorPtr* error,
                                           std::string* out_passphrase) {
  SLOG(this, 2) << __func__;

  Error e;
  std::string passphrase = service_->GetWiFiPassphrase(&e);
  if (!e.IsSuccess()) {
    return !e.ToChromeosError(error);
  }

  *out_passphrase = passphrase;
  return true;
}

}  // namespace shill
