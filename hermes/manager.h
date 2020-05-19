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

#include "hermes/dbus_bindings/org.chromium.Hermes.Manager.h"
#include "hermes/profile.h"

namespace hermes {

class Manager : public org::chromium::Hermes::ManagerInterface,
                public org::chromium::Hermes::ManagerAdaptor {
 public:
  using ByteArray = std::vector<uint8_t>;

  template <typename... T>
  using DBusResponse = brillo::dbus_utils::DBusMethodResponse<T...>;

  Manager(const scoped_refptr<dbus::Bus>& bus, LpaContext* context);

  // org::chromium::Hermes::ManagerInterface overrides.
  // Install a profile. An empty activation code will cause the default profile
  // to be installed.
  void InstallProfileFromActivationCode(
      std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
      const std::string& in_activation_code) override;
  void InstallPendingProfile(
      std::unique_ptr<DBusResponse<>> response,
      const dbus::ObjectPath& in_pending_profile) override;
  void UninstallProfile(std::unique_ptr<DBusResponse<>> response,
                        const dbus::ObjectPath& in_profile) override;
  // Update the PendingProfiles property.
  void RequestPendingEvents(std::unique_ptr<DBusResponse<>> response) override;
  // Set/unset test mode. Normally, only production profiles may be
  // downloaded. In test mode, only test profiles may be downloaded.
  void SetTestMode(bool in_is_test_mode) override;

 private:
  void UpdateInstalledProfilesProperty();

  // Update |profiles_| with all profiles installed on the eUICC.
  void RetrieveInstalledProfiles();

  const scoped_refptr<dbus::Bus>& bus_;
  brillo::dbus_utils::DBusObject dbus_object_;

  LpaContext* context_;

  std::vector<std::unique_ptr<Profile>> installed_profiles_;
  std::vector<std::unique_ptr<Profile>> pending_profiles_;

  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace hermes

#endif  // HERMES_MANAGER_H_
