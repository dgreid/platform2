// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/profile.h"

#include <memory>
#include <string>
#include <utility>

#include "hermes/lpa_util.h"

namespace hermes {

namespace {

const char kBasePath[] = "/org/chromium/Hermes/profile/";

// Function to use as an Lpa callback for a D-Bus method that has no output
// parameters.
//
// Note the use of a shared_ptr rather than unique_ptr. The google-lpa API takes
// std::function parameters as callbacks. Since the standard states that
// std::functions must be CopyConstructible, bind states or lambdas that gain
// ownership of a unique_ptr may not be used as a std::function. As the
// interface is passed a unique_ptr, the options are either to maintain
// DBusMethodResponse lifetimes separately or to convert unique_ptrs to
// shared_ptrs.
void DefaultLpaCallback(
    std::shared_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
    const base::Location& location,
    int error) {
  auto decoded_error = LpaErrorToBrillo(location, error);
  if (decoded_error) {
    response->ReplyWithError(decoded_error.get());
    return;
  }
  response->Return();
}

}  // namespace

Profile::Profile(const scoped_refptr<dbus::Bus>& bus,
                 LpaContext* context,
                 const lpa::proto::ProfileInfo& profile)
    : org::chromium::Hermes::ProfileAdaptor(this),
      object_path_(kBasePath + profile.iccid()),
      dbus_object_(nullptr, bus, object_path_),
      context_(context) {
  CHECK(profile.has_iccid());
  // Initialize properties.
  SetIccid(profile.iccid());
  SetServiceProvider(profile.service_provider_name());
  if (profile.has_profile_owner()) {
    SetMccMnc(profile.profile_owner().mcc() + profile.profile_owner().mnc());
  }
  SetActivationCode(profile.activation_code());
  SetState(profile.profile_state());
  SetProfileClass(profile.profile_class());
  SetName(profile.profile_name());
  SetNickname(profile.profile_nickname());

  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
  LOG(INFO) << "Created Profile: " << object_path_.value();
}

void Profile::Enable(std::unique_ptr<DBusResponse<>> response) {
  context_->lpa->EnableProfile(
      GetIccid(), context_->executor,
      std::bind(&DefaultLpaCallback,
                std::shared_ptr<DBusResponse<>>(std::move(response)), FROM_HERE,
                std::placeholders::_1));
}

void Profile::Disable(std::unique_ptr<DBusResponse<>> response) {
  context_->lpa->DisableProfile(
      GetIccid(), context_->executor,
      std::bind(&DefaultLpaCallback,
                std::shared_ptr<DBusResponse<>>(std::move(response)), FROM_HERE,
                std::placeholders::_1));
}

bool Profile::ValidateNickname(brillo::ErrorPtr* /*error*/,
                               const std::string& value) {
  context_->lpa->SetProfileNickname(
      GetIccid(), value, context_->executor, [](int error) {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          LOG(ERROR) << "Failed to set profile nickname: "
                     << decoded_error->GetMessage();
        }
      });
  return true;
}

}  // namespace hermes
