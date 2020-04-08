// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_PROFILE_H_
#define HERMES_PROFILE_H_

#include <memory>
#include <string>

#include <google-lpa/lpa/core/lpa.h>

#include "hermes/dbus_bindings/org.chromium.Hermes.Profile.h"
#include "hermes/lpa_util.h"

namespace hermes {

class Profile : public org::chromium::Hermes::ProfileInterface,
                public org::chromium::Hermes::ProfileAdaptor {
 public:
  template <typename... T>
  using DBusResponse = brillo::dbus_utils::DBusMethodResponse<T...>;

  Profile(const scoped_refptr<dbus::Bus>& bus,
          LpaContext* context,
          const lpa::proto::ProfileInfo& profile);

  // org::chromium::Hermes::ProfileInterface overrides.
  void Enable(std::unique_ptr<DBusResponse<>> resp) override;
  void Disable(std::unique_ptr<DBusResponse<>> resp) override;

  const dbus::ObjectPath& object_path() const { return object_path_; }

 private:
  // org::chromium::Hermes::ProfileAdaptor override.
  bool ValidateNickname(brillo::ErrorPtr* error,
                        const std::string& value) override;

  dbus::ObjectPath object_path_;
  brillo::dbus_utils::DBusObject dbus_object_;

  LpaContext* context_;
};

}  // namespace hermes

#endif  // HERMES_PROFILE_H_
