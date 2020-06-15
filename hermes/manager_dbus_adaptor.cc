// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/manager_dbus_adaptor.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/manager.h"
#include "hermes/result_callback.h"

using lpa::proto::ProfileInfo;

namespace hermes {

ManagerDBusAdaptor::ManagerDBusAdaptor(Manager* manager)
    : org::chromium::Hermes::ManagerAdaptor(this),
      manager_(manager),
      dbus_object_(nullptr,
                   Context::Get()->bus(),
                   org::chromium::Hermes::ManagerAdaptor::GetObjectPath()) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
  SetPendingProfiles({});
}

void ManagerDBusAdaptor::InstallProfileFromActivationCode(
    std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
    const std::string& in_activation_code,
    const std::string& in_confirmation_code) {
  ResultCallback<dbus::ObjectPath> result_callback(std::move(response));
  manager_->InstallProfileFromActivationCode(
      in_activation_code, in_confirmation_code, std::move(result_callback));
}

void ManagerDBusAdaptor::InstallPendingProfile(
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& /*in_pending_profile*/,
    const std::string& /*in_confirmation_code*/) {
  response->ReplyWithError(
      FROM_HERE, brillo::errors::dbus::kDomain, kErrorUnsupported,
      "This method is not supported until crbug.com/1071470 is implemented");
}

void ManagerDBusAdaptor::UninstallProfile(
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& in_profile) {
  ResultCallback<> result_callback(std::move(response));
  manager_->UninstallProfile(in_profile, std::move(result_callback));
}

void ManagerDBusAdaptor::RequestPendingEvents(
    std::unique_ptr<DBusResponse<>> response) {
  // TODO(crbug.com/1071470) This is stubbed until google-lpa supports SM-DS.
  //
  // Note that there will need to be some way to store the Event Record info
  // (SM-DP+ address and event id) for each pending Profile.
  response->Return();
}

void ManagerDBusAdaptor::SetTestMode(bool /*in_is_test_mode*/) {
  // TODO(akhouderchah) This is a no-op until the Lpa interface allows for
  // switching certificate directory without recreating the Lpa object.
  NOTIMPLEMENTED();
}

}  // namespace hermes
