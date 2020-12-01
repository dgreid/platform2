// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_H_
#define HERMES_EUICC_H_

#include <memory>
#include <string>
#include <vector>

#include <google-lpa/lpa/core/lpa.h>
#include <google-lpa/lpa/data/proto/profile_info.pb.h>

#include "hermes/adaptor_interfaces.h"
#include "hermes/context.h"
#include "hermes/euicc_slot_info.h"
#include "hermes/profile.h"
#include "hermes/result_callback.h"

namespace hermes {

class Euicc {
 public:
  Euicc(uint8_t physical_slot, EuiccSlotInfo slot_info);
  Euicc(const Euicc&) = delete;
  Euicc& operator=(const Euicc&) = delete;

  void UpdateSlotInfo(EuiccSlotInfo slot_info);

  // Install a profile. An empty activation code will cause the default profile
  // to be installed.
  void InstallProfileFromActivationCode(
      const std::string& activation_code,
      const std::string& confirmation_code,
      ResultCallback<dbus::ObjectPath> result_callback);
  void UninstallProfile(const dbus::ObjectPath& profile_path,
                        ResultCallback<> result_callback);
  // Request the eUICC to provide all installed profiles.
  void RequestInstalledProfiles(ResultCallback<> result_callback);

  void InstallPendingProfile(const dbus::ObjectPath& profile_path,
                             const std::string& confirmation_code,
                             ResultCallback<dbus::ObjectPath> result_callback);
  void RequestPendingProfiles(ResultCallback<> result_callback,
                              const std::string& root_smds);

  uint8_t physical_slot() const { return physical_slot_; }
  dbus::ObjectPath object_path() const { return dbus_adaptor_->object_path(); }

 private:
  void OnProfileInstalled(const lpa::proto::ProfileInfo& profile_info,
                          int error,
                          ResultCallback<dbus::ObjectPath> result_callback);
  void OnProfileUninstalled(const dbus::ObjectPath& profile_path,
                            int error,
                            ResultCallback<> result_callback);

  void UpdateInstalledProfilesProperty();

  // Update |installed_profiles_| with all profiles installed on the eUICC.
  void OnInstalledProfilesReceived(
      const std::vector<lpa::proto::ProfileInfo>& profile_infos,
      int error,
      ResultCallback<> result_callback);

  // Update |pending_profiles_| with all profiles installed on the SMDS.
  void UpdatePendingProfilesProperty();
  void OnPendingProfilesReceived(
      const std::vector<lpa::proto::ProfileInfo>& profile_infos,
      int error,
      ResultCallback<> result_callback);

  const uint8_t physical_slot_;
  EuiccSlotInfo slot_info_;

  Context* context_;
  std::unique_ptr<EuiccAdaptorInterface> dbus_adaptor_;

  std::vector<std::unique_ptr<Profile>> installed_profiles_;
  std::vector<std::unique_ptr<Profile>> pending_profiles_;
};

}  // namespace hermes

#endif  // HERMES_EUICC_H_
