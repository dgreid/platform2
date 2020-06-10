// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MANAGER_DBUS_ADAPTOR_H_
#define HERMES_MANAGER_DBUS_ADAPTOR_H_

#include "hermes/dbus_bindings/org.chromium.Hermes.Manager.h"

namespace hermes {

class Manager;

class ManagerDBusAdaptor : public org::chromium::Hermes::ManagerInterface,
                           public org::chromium::Hermes::ManagerAdaptor {
 public:
  explicit ManagerDBusAdaptor(Manager* manager);

  // org::chromium::Hermes::ManagerInterface overrides.
  // Set/unset test mode. Normally, only production profiles may be
  // downloaded. In test mode, only test profiles may be downloaded.
  void SetTestMode(bool in_is_test_mode) override;

 private:
  Manager* manager_;
  brillo::dbus_utils::DBusObject dbus_object_;

  DISALLOW_COPY_AND_ASSIGN(ManagerDBusAdaptor);
};

}  // namespace hermes

#endif  // HERMES_MANAGER_DBUS_ADAPTOR_H_
