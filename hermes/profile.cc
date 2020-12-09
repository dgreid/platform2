// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/profile.h"

#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/executor.h"
#include "hermes/hermes_constants.h"
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

base::Optional<profile::ProfileClass> LpaProfileClassToHermes(
    lpa::proto::ProfileClass cls) {
  switch (cls) {
    case lpa::proto::TESTING:
      return profile::kTesting;
    case lpa::proto::PROVISIONING:
      return profile::kProvisioning;
    case lpa::proto::OPERATIONAL:
      return profile::kOperational;
    default:
      LOG(ERROR) << "Unrecognized lpa ProfileClass: " << cls;
      return base::nullopt;
  }
}

}  // namespace

// static
std::unique_ptr<Profile> Profile::Create(
    const lpa::proto::ProfileInfo& profile_info, const uint32_t physical_slot) {
  CHECK(profile_info.has_iccid());
  auto profile = std::unique_ptr<Profile>(new Profile(
      dbus::ObjectPath(kBasePath + profile_info.iccid()), physical_slot));

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
  auto cls = LpaProfileClassToHermes(profile_info.profile_class());
  if (!cls.has_value()) {
    LOG(ERROR) << "Failed to create Profile for iccid " << profile_info.iccid()
               << "; invalid ProfileClass " << profile_info.profile_class();
    return nullptr;
  }
  profile->SetProfileClass(cls.value());
  profile->SetName(profile_info.profile_name());
  profile->SetNickname(profile_info.profile_nickname());

  profile->RegisterWithDBusObject(&profile->dbus_object_);
  profile->dbus_object_.RegisterAndBlock();

  LOG(INFO) << "Created Profile: " << profile->object_path_.value()
            << " on slot: " << profile->physical_slot_;
  return profile;
}

Profile::Profile(dbus::ObjectPath object_path, const uint32_t physical_slot)
    : org::chromium::Hermes::ProfileAdaptor(this),
      context_(Context::Get()),
      object_path_(std::move(object_path)),
      dbus_object_(nullptr, context_->bus(), object_path_),
      physical_slot_(physical_slot),
      weak_factory_(this) {}

void Profile::Enable(std::unique_ptr<DBusResponse<>> response) {
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::Enable, weak_factory_.GetWeakPtr(),
                       std::move(response)),
        kLpaRetryDelay);
    return;
  }
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot enable a pending Profile object");
    return;
  }

  LOG(INFO) << "Enabling profile: " << object_path_.value();
  context_->modem_control()->StartProfileOp(physical_slot_);
  context_->lpa()->EnableProfile(
      GetIccid(), context_->executor(),
      [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
       weak{weak_factory_.GetWeakPtr()}](int error) mutable {
        if (weak) {
          weak->context_->modem_control()->FinishProfileOp();
          weak->OnEnabled(error, std::move(response));
        }
      });
}

void Profile::Disable(std::unique_ptr<DBusResponse<>> response) {
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::Disable, weak_factory_.GetWeakPtr(),
                       std::move(response)),
        kLpaRetryDelay);
    return;
  }
  if (GetState() == profile::kPending) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorPendingProfile,
                             "Cannot disable a pending Profile object");
    return;
  }

  LOG(INFO) << "Disabling profile: " << object_path_.value();
  context_->modem_control()->StartProfileOp(physical_slot_);
  context_->lpa()->DisableProfile(
      GetIccid(), context_->executor(),
      [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
       weak{weak_factory_.GetWeakPtr()}](int error) mutable {
        if (weak) {
          weak->context_->modem_control()->FinishProfileOp();
          weak->OnDisabled(error, std::move(response));
        }
      });
}

void Profile::OnEnabled(int error, std::shared_ptr<DBusResponse<>> response) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(INFO) << "Failed enabling profile: " << object_path_.value()
              << " (error " << decoded_error << ")";
    response->ReplyWithError(decoded_error.get());
    return;
  }
  LOG(INFO) << "Enabled profile: " << object_path_.value();
  SetState(profile::kActive);
  response->Return();
}

void Profile::OnDisabled(int error, std::shared_ptr<DBusResponse<>> response) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(INFO) << "Failed disabling profile: " << object_path_.value()
              << " (error " << decoded_error << ")";
    response->ReplyWithError(decoded_error.get());
    return;
  }
  LOG(INFO) << "Disabled profile: " << object_path_.value();
  SetState(profile::kInactive);
  response->Return();
}

void Profile::SetProfileNickname(std::string nickname) {
  if (!context_->lpa()->IsLpaIdle()) {
    context_->executor()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&Profile::SetProfileNickname, weak_factory_.GetWeakPtr(),
                       std::move(nickname)),
        kLpaRetryDelay);
    return;
  }
  context_->modem_control()->StoreAndSetActiveSlot(physical_slot_);
  context_->lpa()->SetProfileNickname(
      GetIccid(), nickname, context_->executor(), [this](int error) {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          LOG(ERROR) << "Failed to set profile nickname: "
                     << decoded_error->GetMessage();
        }
        context_->modem_control()->RestoreActiveSlot();
      });
}

bool Profile::ValidateNickname(brillo::ErrorPtr* /*error*/,
                               const std::string& value) {
  SetProfileNickname(value);
  return true;
}

Profile::~Profile() {
  dbus_object_.UnregisterAsync();
}

}  // namespace hermes
