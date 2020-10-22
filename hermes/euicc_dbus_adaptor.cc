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
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& /*in_pending_profile*/,
    const std::string& /*in_confirmation_code*/) {
  response->ReplyWithError(
      FROM_HERE, brillo::errors::dbus::kDomain, kErrorUnsupported,
      "This method is not supported until crbug.com/1071470 is implemented");
}

void EuiccDBusAdaptor::UninstallProfile(
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& in_profile) {
  ResultCallback<> result_callback(std::move(response));
  euicc_->UninstallProfile(in_profile, std::move(result_callback));
}

void EuiccDBusAdaptor::RequestPendingEvents(
    std::unique_ptr<DBusResponse<>> response) {
  // TODO(crbug.com/1071470) This is stubbed until google-lpa supports SM-DS.
  //
  // Note that there will need to be some way to store the Event Record info
  // (SM-DP+ address and event id) for each pending Profile.
  response->Return();
}

void EuiccDBusAdaptor::RequestInstalledProfiles(
    std::unique_ptr<DBusResponse<>> response) {
  ResultCallback<> result_callback(std::move(response));
  euicc_->RequestInstalledProfiles(std::move(result_callback));
}
}  // namespace hermes
