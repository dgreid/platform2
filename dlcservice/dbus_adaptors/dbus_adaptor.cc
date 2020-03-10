// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dbus_adaptors/dbus_adaptor.h"

#include <memory>
#include <string>
#include <utility>

#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

using std::string;
using std::unique_ptr;

namespace dlcservice {

DBusService::DBusService(DlcService* dlc_service) : dlc_service_(dlc_service) {}

bool DBusService::Install(brillo::ErrorPtr* err,
                          const DlcModuleList& dlc_module_list_in) {
  return dlc_service_->Install(dlc_module_list_in, err);
}

bool DBusService::Uninstall(brillo::ErrorPtr* err, const string& id_in) {
  return dlc_service_->Uninstall(id_in, err);
}

bool DBusService::GetInstalled(brillo::ErrorPtr* err,
                               DlcModuleList* dlc_module_list_out) {
  return dlc_service_->GetInstalled(dlc_module_list_out, err);
}

DBusAdaptor::DBusAdaptor(unique_ptr<DBusService> dbus_service)
    : org::chromium::DlcServiceInterfaceAdaptor(dbus_service.get()),
      dbus_service_(std::move(dbus_service)) {}

void DBusAdaptor::SendInstallStatus(const InstallStatus& status) {
  SendOnInstallStatusSignal(status);
}

}  // namespace dlcservice
