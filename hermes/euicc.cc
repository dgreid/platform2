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

namespace {
const char kDefaultRootSmds[] = "lpa.ds.gsma.com";
}

namespace hermes {

Euicc::Euicc(uint8_t physical_slot, EuiccSlotInfo slot_info)
    : physical_slot_(physical_slot),
      slot_info_(std::move(slot_info)),
      context_(Context::Get()),
      dbus_adaptor_(context_->adaptor_factory()->CreateEuiccAdaptor(this)) {
  dbus_adaptor_->SetPendingProfiles({});
  UpdateSlotInfo(slot_info_);
}

void Euicc::UpdateSlotInfo(EuiccSlotInfo slot_info) {
  slot_info_ = std::move(slot_info);
  dbus_adaptor_->SetEid(slot_info_.eid());
  dbus_adaptor_->SetIsActive(slot_info_.IsActive());
}

void Euicc::UpdateLogicalSlot(base::Optional<uint8_t> logical_slot) {
  slot_info_.SetLogicalSlot(std::move(logical_slot));
  dbus_adaptor_->SetIsActive(slot_info_.IsActive());
}

void Euicc::InstallProfileFromActivationCode(
    const std::string& activation_code,
    const std::string& confirmation_code,
    ResultCallback<dbus::ObjectPath> result_callback) {
  auto profile_cb = [result_callback{std::move(result_callback)}, this](
                        lpa::proto::ProfileInfo& info, int error) mutable {
    OnProfileInstalled(info, error, std::move(result_callback));
  };
  context_->modem_control()->StoreAndSetActiveSlot(physical_slot_);
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

void Euicc::InstallPendingProfile(
    const dbus::ObjectPath& profile_path,
    const std::string& confirmation_code,
    ResultCallback<dbus::ObjectPath> result_callback) {
  auto iter = find_if(pending_profiles_.begin(), pending_profiles_.end(),
                      [&profile_path](const std::unique_ptr<Profile>& profile) {
                        return profile->object_path() == profile_path;
                      });

  if (iter == pending_profiles_.end()) {
    result_callback.Error(brillo::Error::Create(
        FROM_HERE, brillo::errors::dbus::kDomain, kErrorInvalidParameter,
        "Could not find Profile " + profile_path.value()));
    return;
  }

  std::string activation_code = iter->get()->GetActivationCode();
  InstallProfileFromActivationCode(activation_code, confirmation_code,
                                   std::move(result_callback));
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

  context_->modem_control()->StoreAndSetActiveSlot(physical_slot_);
  context_->lpa()->DeleteProfile(
      matching_profile->GetIccid(), context_->executor(),
      [result_callback{std::move(result_callback)}, profile_path,
       this](int error) mutable {
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

void Euicc::UpdatePendingProfilesProperty() {
  std::vector<dbus::ObjectPath> profile_paths;
  for (auto& profile : pending_profiles_) {
    profile_paths.push_back(profile->object_path());
  }
  dbus_adaptor_->SetPendingProfiles(profile_paths);
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

  auto iter = find_if(pending_profiles_.begin(), pending_profiles_.end(),
                      [&profile_info](const std::unique_ptr<Profile>& profile) {
                        return profile->GetIccid() == profile_info.iccid();
                      });

  std::unique_ptr<Profile> profile;
  if (iter != pending_profiles_.end()) {
    // Remove the profile from pending_profiles_ so that it can become an
    // installed profile
    profile = std::move(*iter);
    pending_profiles_.erase(iter);
    UpdatePendingProfilesProperty();
  } else {
    profile = Profile::Create(profile_info, physical_slot_);
  }

  if (!profile) {
    result_callback.Error(brillo::Error::Create(
        FROM_HERE, brillo::errors::dbus::kDomain, kErrorInternalLpaFailure,
        "Failed to create Profile object"));
    return;
  }

  installed_profiles_.push_back(std::move(profile));
  UpdateInstalledProfilesProperty();
  // Refresh LPA profile cache
  context_->lpa()->GetInstalledProfiles(
      context_->executor(),
      [result_callback{std::move(result_callback)}, this](
          std::vector<lpa::proto::ProfileInfo>& profile_infos, int error) {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          result_callback.Error(decoded_error);
          return;
        }
        result_callback.Success(installed_profiles_.back()->object_path());
      });
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
  // Refresh LPA profile cache
  context_->lpa()->GetInstalledProfiles(
      context_->executor(),
      [result_callback{std::move(result_callback)}](
          std::vector<lpa::proto::ProfileInfo>& profile_infos, int error) {
        auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
        if (decoded_error) {
          result_callback.Error(decoded_error);
          return;
        }
        result_callback.Success();
      });
}

void Euicc::RequestInstalledProfiles(ResultCallback<> result_callback) {
  context_->modem_control()->StoreAndSetActiveSlot(physical_slot_);
  context_->lpa()->GetInstalledProfiles(
      context_->executor(),
      [result_callback{std::move(result_callback)}, this](
          std::vector<lpa::proto::ProfileInfo>& profile_infos,
          int error) mutable {
        OnInstalledProfilesReceived(profile_infos, error,
                                    std::move(result_callback));
      });
}

void Euicc::OnInstalledProfilesReceived(
    const std::vector<lpa::proto::ProfileInfo>& profile_infos,
    int error,
    ResultCallback<> result_callback) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(ERROR) << "Failed to retrieve installed profiles";
    result_callback.Error(decoded_error);
    return;
  }
  installed_profiles_.clear();
  UpdateInstalledProfilesProperty();
  for (const auto& info : profile_infos) {
    auto profile = Profile::Create(info, physical_slot_);
    if (profile) {
      installed_profiles_.push_back(std::move(profile));
    }
  }
  UpdateInstalledProfilesProperty();
  result_callback.Success();
}

void Euicc::RequestPendingProfiles(ResultCallback<> result_callback,
                                   const std::string& root_smds) {
  context_->modem_control()->StoreAndSetActiveSlot(physical_slot_);
  context_->lpa()->GetPendingProfilesFromSmds(
      root_smds.empty() ? kDefaultRootSmds : root_smds, context_->executor(),
      [result_callback{std::move(result_callback)}, this](
          std::vector<lpa::proto::ProfileInfo>& profile_infos,
          int error) mutable {
        OnPendingProfilesReceived(profile_infos, error,
                                  std::move(result_callback));
      });
}

void Euicc::OnPendingProfilesReceived(
    const std::vector<lpa::proto::ProfileInfo>& profile_infos,
    int error,
    ResultCallback<> result_callback) {
  auto decoded_error = LpaErrorToBrillo(FROM_HERE, error);
  if (decoded_error) {
    LOG(ERROR) << "Failed to retrieve pending profiles";
    result_callback.Error(decoded_error);
    return;
  }

  pending_profiles_.clear();
  UpdatePendingProfilesProperty();
  for (const auto& info : profile_infos) {
    auto profile = Profile::Create(info, physical_slot_);
    if (profile) {
      pending_profiles_.push_back(std::move(profile));
    }
  }
  UpdatePendingProfilesProperty();
  result_callback.Success();
}

}  // namespace hermes
