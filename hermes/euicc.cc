// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/euicc.h"

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

Euicc::Euicc(uint8_t physical_slot, EuiccSlotInfo slot_info)
    : physical_slot_(physical_slot),
      context_(Context::Get()),
      dbus_adaptor_(context_->adaptor_factory()->CreateEuiccAdaptor(this)) {
  dbus_adaptor_->SetPendingProfiles({});
  UpdateSlotInfo(std::move(slot_info));
}

void Euicc::UpdateSlotInfo(EuiccSlotInfo slot_info) {
  bool was_active = slot_info_.IsActive();
  bool is_active = slot_info.IsActive();

  slot_info_ = std::move(slot_info);
  if (was_active == is_active) {
    return;
  }

  dbus_adaptor_->SetIsActive(is_active);
  if (is_active) {
    RequestInstalledProfiles();
  }
}

void Euicc::InstallProfileFromActivationCode(
    const std::string& activation_code,
    const std::string& confirmation_code,
    ResultCallback<dbus::ObjectPath> result_callback) {
  auto profile_cb = [result_callback{std::move(result_callback)}, this](
                        lpa::proto::ProfileInfo& info, int error) {
    OnProfileInstalled(info, error, std::move(result_callback));
  };
  if (activation_code.empty()) {
    context_->lpa()->GetDefaultProfileFromSmdp("", context_->executor(),
                                               std::move(profile_cb));
    return;
  }

  lpa::core::Lpa::DownloadOptions options;
  options.enable_profile = false;
  options.allow_policy_rules = false;
  options.confirmation_code = confirmation_code;
  context_->lpa()->DownloadProfile(activation_code, std::move(options),
                                   context_->executor(), std::move(profile_cb));
}

void Euicc::UninstallProfile(const dbus::ObjectPath& profile_path,
                             ResultCallback<> result_callback) {
  const Profile* matching_profile = nullptr;
  for (auto& profile : installed_profiles_) {
    if (profile->object_path() == profile_path) {
      matching_profile = profile.get();
      break;
    }
  }
  if (!matching_profile) {
    result_callback.Error(brillo::Error::Create(
        FROM_HERE, brillo::errors::dbus::kDomain, kErrorInvalidParameter,
        "Could not find Profile " + profile_path.value()));
    return;
  }

  context_->lpa()->DeleteProfile(
      matching_profile->GetIccid(), context_->executor(),
      [result_callback{std::move(result_callback)}, profile_path,
       this](int error) {
        OnProfileUninstalled(profile_path, error, std::move(result_callback));
      });
}

void Euicc::UpdateInstalledProfilesProperty() {
  std::vector<dbus::ObjectPath> profile_paths;
  for (auto& profile : installed_profiles_) {
    profile_paths.push_back(profile->object_path());
  }
  dbus_adaptor_->SetInstalledProfiles(profile_paths);
}

void Euicc::OnProfileInstalled(
    const lpa::proto::ProfileInfo& profile_info,
    int error,
    ResultCallback<dbus::ObjectPath> result_callback) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    result_callback.Error(decoded_error);
    return;
  }

  auto profile = Profile::Create(profile_info);
  if (!profile) {
    result_callback.Error(brillo::Error::Create(
        FROM_HERE, brillo::errors::dbus::kDomain, kErrorInternalLpaFailure,
        "Failed to create Profile object"));
    return;
  }

  installed_profiles_.push_back(std::move(profile));
  UpdateInstalledProfilesProperty();
  result_callback.Success(installed_profiles_.back()->object_path());
}

void Euicc::OnProfileUninstalled(const dbus::ObjectPath& profile_path,
                                 int error,
                                 ResultCallback<> result_callback) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    result_callback.Error(decoded_error);
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
  result_callback.Success();
}

void Euicc::RequestInstalledProfiles() {
  context_->lpa()->GetInstalledProfiles(
      context_->executor(),
      [this](std::vector<lpa::proto::ProfileInfo>& profile_infos, int error) {
        OnInstalledProfilesReceived(profile_infos, error);
      });
}

void Euicc::OnInstalledProfilesReceived(
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
