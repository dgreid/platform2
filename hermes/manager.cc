// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/manager.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/executor.h"
#include "hermes/lpa_util.h"

using lpa::proto::ProfileInfo;

namespace hermes {

Manager::Manager()
    : org::chromium::Hermes::ManagerAdaptor(this),
      context_(Context::Get()),
      dbus_object_(nullptr,
                   context_->bus(),
                   org::chromium::Hermes::ManagerAdaptor::GetObjectPath()) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();

  SetPendingProfiles({});
  RequestInstalledProfiles();
}

void Manager::InstallProfileFromActivationCode(
    std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
    const std::string& in_activation_code,
    const std::string& in_confirmation_code) {
  auto profile_cb = [response{std::shared_ptr<DBusResponse<dbus::ObjectPath>>(
                         std::move(response))},
                     this](lpa::proto::ProfileInfo& info, int error) {
    OnProfileInstalled(info, error, std::move(response));
  };
  if (in_activation_code.empty()) {
    context_->lpa()->GetDefaultProfileFromSmdp("", context_->executor(),
                                               std::move(profile_cb));
    return;
  }

  lpa::core::Lpa::DownloadOptions options;
  options.enable_profile = false;
  options.allow_policy_rules = false;
  options.confirmation_code = in_confirmation_code;
  context_->lpa()->DownloadProfile(in_activation_code, std::move(options),
                                   context_->executor(), std::move(profile_cb));
}

void Manager::InstallPendingProfile(
    std::unique_ptr<DBusResponse<>> response,
    const dbus::ObjectPath& /*in_pending_profile*/,
    const std::string& /*in_confirmation_code*/) {
  response->ReplyWithError(
      FROM_HERE, brillo::errors::dbus::kDomain, kErrorUnsupported,
      "This method is not supported until crbug.com/1071470 is implemented");
}

void Manager::UninstallProfile(std::unique_ptr<DBusResponse<>> response,
                               const dbus::ObjectPath& in_profile) {
  const Profile* matching_profile = nullptr;
  for (auto& profile : installed_profiles_) {
    if (profile->object_path() == in_profile) {
      matching_profile = profile.get();
      break;
    }
  }
  if (!matching_profile) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorInvalidParameter,
                             "Could not find Profile " + in_profile.value());
    return;
  }

  context_->lpa()->DeleteProfile(
      matching_profile->GetIccid(), context_->executor(),
      [response{std::shared_ptr<DBusResponse<>>(std::move(response))},
       in_profile, this](int error) {
        OnProfileUninstalled(in_profile, error, std::move(response));
      });
}

void Manager::RequestPendingEvents(std::unique_ptr<DBusResponse<>> response) {
  // TODO(crbug.com/1071470) This is stubbed until google-lpa supports SM-DS.
  //
  // Note that there will need to be some way to store the Event Record info
  // (SM-DP+ address and event id) for each pending Profile.
  response->Return();
}

void Manager::SetTestMode(bool /*in_is_test_mode*/) {
  // TODO(akhouderchah) This is a no-op until the Lpa interface allows for
  // switching certificate directory without recreating the Lpa object.
  NOTIMPLEMENTED();
}

void Manager::UpdateInstalledProfilesProperty() {
  std::vector<dbus::ObjectPath> profile_paths;
  for (auto& profile : installed_profiles_) {
    profile_paths.push_back(profile->object_path());
  }
  SetInstalledProfiles(profile_paths);
}

void Manager::OnProfileInstalled(
    const lpa::proto::ProfileInfo& profile_info,
    int error,
    std::shared_ptr<DBusResponse<dbus::ObjectPath>> response) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    response->ReplyWithError(decoded_error.get());
    return;
  }

  auto profile = Profile::Create(profile_info);
  if (!profile) {
    response->ReplyWithError(FROM_HERE, brillo::errors::dbus::kDomain,
                             kErrorInternalLpaFailure,
                             "Failed to create Profile object");
    return;
  }

  installed_profiles_.push_back(std::move(profile));
  UpdateInstalledProfilesProperty();
  response->Return(installed_profiles_.back()->object_path());
}

void Manager::OnProfileUninstalled(const dbus::ObjectPath& profile_path,
                                   int error,
                                   std::shared_ptr<DBusResponse<>> response) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    response->ReplyWithError(decoded_error.get());
    return;
  }

  auto iter = installed_profiles_.begin();
  for (; iter != installed_profiles_.end(); ++iter) {
    if ((*iter)->object_path() == profile_path) {
      break;
    }
  }
  CHECK(iter != installed_profiles_.end());
  installed_profiles_.erase(iter);
  UpdateInstalledProfilesProperty();
  response->Return();
}

void Manager::RequestInstalledProfiles() {
  context_->lpa()->GetInstalledProfiles(
      context_->executor(),
      [this](std::vector<lpa::proto::ProfileInfo>& profile_infos, int error) {
        OnInstalledProfilesReceived(profile_infos, error);
      });
}

void Manager::OnInstalledProfilesReceived(
    const std::vector<lpa::proto::ProfileInfo>& profile_infos, int error) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(ERROR) << "Failed to retrieve installed profiles";
    return;
  }

  for (const auto& info : profile_infos) {
    auto profile = Profile::Create(info);
    if (profile) {
      installed_profiles_.push_back(std::move(profile));
    }
  }
  UpdateInstalledProfilesProperty();
}

}  // namespace hermes
