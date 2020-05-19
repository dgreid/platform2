// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/profile.h"

#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/lpa_util.h"

namespace hermes {

namespace {

const char kBasePath[] = "/org/chromium/Hermes/profile/";

base::Optional<profile::State> LpaProfileStateToHermes(
    lpa::proto::ProfileState state) {
  switch (state) {
    case lpa::proto::DISABLED:
      return profile::kInactive;
    case lpa::proto::ENABLED:
      return profile::kActive;
    default:
      LOG(ERROR) << "Unrecognized lpa ProfileState: " << state;
      return base::nullopt;
  }
}

}  // namespace

// static
std::unique_ptr<Profile> Profile::Create(
    const scoped_refptr<dbus::Bus>& bus,
    LpaContext* lpa_context,
    const lpa::proto::ProfileInfo& profile_info) {
  CHECK(profile_info.has_iccid());
  auto profile = std::unique_ptr<Profile>(
      new Profile(bus, dbus::ObjectPath(kBasePath + profile_info.iccid())));

  // Initialize properties.
  profile->SetIccid(profile_info.iccid());
  profile->SetServiceProvider(profile_info.service_provider_name());
  if (profile_info.has_profile_owner()) {
    profile->SetMccMnc(profile_info.profile_owner().mcc() +
                       profile_info.profile_owner().mnc());
  }
  profile->SetActivationCode(profile_info.activation_code());
  auto state = LpaProfileStateToHermes(profile_info.profile_state());
  if (!state.has_value()) {
    LOG(ERROR) << "Failed to create Profile for iccid " << profile_info.iccid()
               << "; invalid ProfileState " << profile_info.profile_state();
    return nullptr;
  }
  profile->SetState(state.value());
  profile->SetProfileClass(profile_info.profile_class());
  profile->SetName(profile_info.profile_name());
  profile->SetNickname(profile_info.profile_nickname());

  profile->context_ = lpa_context;
  profile->RegisterWithDBusObject(&profile->dbus_object_);
  profile->dbus_object_.RegisterAndBlock();

  LOG(INFO) << "Created Profile: " << profile->object_path_.value();
  return profile;
}

Profile::Profile(const scoped_refptr<dbus::Bus>& bus,
                 dbus::ObjectPath object_path)
    : org::chromium::Hermes::ProfileAdaptor(this),
      object_path_(std::move(object_path)),
      dbus_object_(nullptr, bus, object_path_) {}

void Profile::Enable(std::unique_ptr<DBusResponse<>> response) {
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot enable a pending Profile object");
    return;
  }

  auto cb = [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
             this](int error) {
    auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
    if (decoded_error) {
      response->ReplyWithError(decoded_error.get());
      return;
    }
    SetState(profile::kActive);
    response->Return();
  };
  context_->lpa->EnableProfile(GetIccid(), context_->executor, std::move(cb));
}

void Profile::Disable(std::unique_ptr<DBusResponse<>> response) {
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot disable a pending Profile object");
    return;
  }

  auto cb = [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
             this](int error) {
    auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
    if (decoded_error) {
      response->ReplyWithError(decoded_error.get());
      return;
    }
    SetState(profile::kInactive);
    response->Return();
  };
  context_->lpa->DisableProfile(GetIccid(), context_->executor, std::move(cb));
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
