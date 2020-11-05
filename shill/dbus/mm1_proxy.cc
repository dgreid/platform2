// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/mm1_proxy.h"

#include <tuple>

#include "shill/cellular/cellular_error.h"
#include "shill/logging.h"

using std::string;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
static string ObjectID(const dbus::ObjectPath* p) {
  return p->value();
}
}  // namespace Logging

namespace mm1 {

Mm1Proxy::Mm1Proxy(const scoped_refptr<dbus::Bus>& bus, const string& service)
    : proxy_(new org::freedesktop::ModemManager1Proxy(bus, service)) {}

Mm1Proxy::~Mm1Proxy() = default;

void Mm1Proxy::ScanDevices(const ResultCallback& callback, int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__;
  proxy_->ScanDevicesAsync(
      base::Bind(&Mm1Proxy::OnOperationSuccess, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      base::Bind(&Mm1Proxy::OnOperationFailure, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      timeout);
}

void Mm1Proxy::SetLogging(const std::string& level,
                          const ResultCallback& callback,
                          int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << ": " << level;
  proxy_->SetLoggingAsync(
      level,
      base::Bind(&Mm1Proxy::OnOperationSuccess, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      base::Bind(&Mm1Proxy::OnOperationFailure, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      timeout);
}

void Mm1Proxy::InhibitDevice(const std::string& uid,
                             bool inhibit,
                             const ResultCallback& callback,
                             int timeout) {
  SLOG(&proxy_->GetObjectPath(), 2)
      << __func__ << ": " << uid << " = " << inhibit;
  proxy_->InhibitDeviceAsync(
      uid, inhibit,
      base::Bind(&Mm1Proxy::OnOperationSuccess, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      base::Bind(&Mm1Proxy::OnOperationFailure, weak_factory_.GetWeakPtr(),
                 callback, __func__),
      timeout);
}

void Mm1Proxy::OnOperationSuccess(const ResultCallback& callback,
                                  const string& operation) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << ": " << operation;
  callback.Run(Error());
}

void Mm1Proxy::OnOperationFailure(const ResultCallback& callback,
                                  const string& operation,
                                  brillo::Error* dbus_error) {
  SLOG(&proxy_->GetObjectPath(), 2) << __func__ << ": " << operation;
  Error error;
  CellularError::FromMM1ChromeosDBusError(dbus_error, &error);
  callback.Run(error);
}

}  // namespace mm1
}  // namespace shill
