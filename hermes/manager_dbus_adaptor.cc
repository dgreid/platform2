// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/manager_dbus_adaptor.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

#include "hermes/manager.h"
#include "hermes/result_callback.h"

using lpa::proto::ProfileInfo;

namespace hermes {

ManagerDBusAdaptor::ManagerDBusAdaptor(Manager* manager)
    : ManagerAdaptorInterface(this),
      manager_(manager),
      dbus_object_(nullptr,
                   Context::Get()->bus(),
                   org::chromium::Hermes::ManagerAdaptor::GetObjectPath()) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAndBlock();
}

void ManagerDBusAdaptor::SetTestMode(bool in_is_test_mode) {
  manager_->SetTestMode(in_is_test_mode);
}

}  // namespace hermes
