// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_DBUS_ADAPTOR_H_
#define HERMES_EUICC_DBUS_ADAPTOR_H_

#include <memory>
#include <string>

#include "hermes/adaptor_interfaces.h"

namespace hermes {

class Euicc;

class EuiccDBusAdaptor : public EuiccAdaptorInterface,
                         public org::chromium::Hermes::EuiccInterface {
 public:
  template <typename... T>
  using DBusResponse = brillo::dbus_utils::DBusMethodResponse<T...>;

  explicit EuiccDBusAdaptor(Euicc* euicc);
  EuiccDBusAdaptor(const EuiccDBusAdaptor&) = delete;
  EuiccDBusAdaptor& operator=(const EuiccDBusAdaptor&) = delete;

  // org::chromium::Hermes::EuiccInterface overrides.
  // Install a profile. An empty activation code will cause the default profile
  // to be installed.
  void InstallProfileFromActivationCode(
      std::unique_ptr<DBusResponse<dbus::ObjectPath>> response,
      const std::string& in_activation_code,
      const std::string& in_confirmation_code) override;
  void InstallPendingProfile(std::unique_ptr<DBusResponse<>> response,
                             const dbus::ObjectPath& in_pending_profile,
                             const std::string& in_confirmation_code) override;
  void UninstallProfile(std::unique_ptr<DBusResponse<>> response,
                        const dbus::ObjectPath& in_profile) override;
  // Update the PendingProfiles property.
  void RequestPendingEvents(std::unique_ptr<DBusResponse<>> response) override;
  void RequestInstalledProfiles(
      std::unique_ptr<DBusResponse<>> response) override;

  // EuiccAdaptorInterface override.
  dbus::ObjectPath object_path() const override { return object_path_; }

 private:
  // Id for next created Euicc object.
  static uint16_t next_id_;

  Euicc* euicc_;
  dbus::ObjectPath object_path_;
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace hermes

#endif  // HERMES_EUICC_DBUS_ADAPTOR_H_
