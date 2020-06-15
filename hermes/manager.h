// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MANAGER_H_
#define HERMES_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include <google-lpa/lpa/core/lpa.h>
#include <google-lpa/lpa/data/proto/profile_info.pb.h>

#include "hermes/context.h"
#include "hermes/dbus_bindings/org.chromium.Hermes.Manager.h"
#include "hermes/profile.h"
#include "hermes/result_callback.h"

namespace hermes {

class Manager {
 public:
  Manager();

  // org::chromium::Hermes::ManagerInterface overrides.
  // Install a profile. An empty activation code will cause the default profile
  // to be installed.
  void InstallProfileFromActivationCode(
      const std::string& in_activation_code,
      const std::string& in_confirmation_code,
      ResultCallback<dbus::ObjectPath> result_callback);
  void InstallPendingProfile(const dbus::ObjectPath& in_pending_profile,
                             const std::string& in_confirmation_code);

  void UninstallProfile(const dbus::ObjectPath& in_profile,
                        ResultCallback<> result_callback);

 private:
  void OnProfileInstalled(const lpa::proto::ProfileInfo& profile_info,
                          int error,
                          ResultCallback<dbus::ObjectPath> result_callback);
  void OnProfileUninstalled(const dbus::ObjectPath& profile_path,
                            int error,
                            ResultCallback<> result_callback);
  void UpdateInstalledProfilesProperty();

  // Request the eUICC to provide all installed profiles.
  void RequestInstalledProfiles();
  // Update |profiles_| with all profiles installed on the eUICC.
  void OnInstalledProfilesReceived(
      const std::vector<lpa::proto::ProfileInfo>& profile_infos, int error);

  Context* context_;

  std::unique_ptr<org::chromium::Hermes::ManagerAdaptor> dbus_adaptor_;

  std::vector<std::unique_ptr<Profile>> installed_profiles_;
  std::vector<std::unique_ptr<Profile>> pending_profiles_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace hermes

#endif  // HERMES_MANAGER_H_
