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

#include "dlcservice/dlc.h"
#include "dlcservice/utils.h"

using std::string;
using std::unique_ptr;

namespace dlcservice {

namespace {
// Converts a |DlcModuleList| into a |DlcSet| based on filtering logic where
// a return value of true indicates insertion into |DlcSet|.
DlcSet ToDlcSet(const dlcservice::DlcModuleList& dlc_module_list,
                const std::function<bool(const DlcModuleInfo&)>& filter) {
  DlcSet s;
  for (const DlcModuleInfo& dlc_module : dlc_module_list.dlc_module_infos()) {
    if (filter(dlc_module))
      s.insert(dlc_module.dlc_id());
  }
  return s;
}
};  // namespace

DBusService::DBusService(DlcService* dlc_service) : dlc_service_(dlc_service) {}

bool DBusService::Install(brillo::ErrorPtr* err,
                          const DlcModuleList& dlc_module_list_in) {
  const auto dlcs =
      ToDlcSet(dlc_module_list_in, [](const DlcModuleInfo&) { return true; });

  return dlc_service_->Install(dlcs, dlc_module_list_in.omaha_url(), err);
}

bool DBusService::Uninstall(brillo::ErrorPtr* err, const string& id_in) {
  return dlc_service_->Uninstall(id_in, err);
}

bool DBusService::GetInstalled(brillo::ErrorPtr* err,
                               DlcModuleList* dlc_module_list_out) {
  DlcSet ids = dlc_service_->GetInstalled();
  for (const auto& id : ids) {
    auto* dlc_info = dlc_module_list_out->add_dlc_module_infos();
    dlc_info->set_dlc_id(id);
    dlc_info->set_dlc_root(dlc_service_->GetDlc(id).GetRoot().value());
  }
  return true;
}

bool DBusService::GetState(brillo::ErrorPtr* err,
                           const string& id_in,
                           DlcState* dlc_state_out) {
  return dlc_service_->GetState(id_in, dlc_state_out, err);
}

DBusAdaptor::DBusAdaptor(unique_ptr<DBusService> dbus_service)
    : org::chromium::DlcServiceInterfaceAdaptor(dbus_service.get()),
      dbus_service_(std::move(dbus_service)) {}

void DBusAdaptor::SendInstallStatus(const InstallStatus& status) {
  SendOnInstallStatusSignal(status);
}

}  // namespace dlcservice
