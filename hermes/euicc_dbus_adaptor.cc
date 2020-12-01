// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/euicc_dbus_adaptor.h"

#include <memory>
#include <string>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/euicc.h"
#include "hermes/result_callback.h"

namespace hermes {

namespace {

const char kBasePath[] = "/org/chromium/Hermes/euicc/";

}  // namespace

// static
uint16_t EuiccDBusAdaptor::next_id_ = 0;

EuiccDBusAdaptor::EuiccDBusAdaptor(Euicc* euicc)
    : EuiccAdaptorInterface(this),
      euicc_(euicc),
      object_path_(kBasePath + base::NumberToString(next_id_++)),
      dbus_object_(nullptr, Context::Get()->bus(), object_path_) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

void EuiccDBusAdaptor::InstallProfileFromActivationCode(
    std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
    const std::string& in_activation_code,
    const std::string& in_confirmation_code) {
  ResultCallback<dbus::ObjectPath> result_callback(std::move(response));
  euicc_->InstallProfileFromActivationCode(
      in_activation_code, in_confirmation_code, std::move(result_callback));
}

void EuiccDBusAdaptor::InstallPendingProfile(
    std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
    const dbus::ObjectPath& in_pending_profile,
    const std::string& in_confirmation_code) {
  ResultCallback<dbus::ObjectPath> result_callback(std::move(response));
  euicc_->InstallPendingProfile(in_pending_profile, in_confirmation_code,
                                std::move(result_callback));
}

void EuiccDBusAdaptor::UninstallProfile(
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& in_profile) {
  ResultCallback<> result_callback(std::move(response));
  euicc_->UninstallProfile(in_profile, std::move(result_callback));
}

void EuiccDBusAdaptor::RequestPendingProfiles(
    std::unique_ptr<DBusResponse<>> response, const std::string& in_root_smds) {
  ResultCallback<> result_callback(std::move(response));
  euicc_->RequestPendingProfiles(std::move(result_callback), in_root_smds);
}

void EuiccDBusAdaptor::RequestInstalledProfiles(
    std::unique_ptr<DBusResponse<>> response) {
  ResultCallback<> result_callback(std::move(response));
  euicc_->RequestInstalledProfiles(std::move(result_callback));
}
}  // namespace hermes
