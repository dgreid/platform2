// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DBUS_ADAPTORS_DBUS_ADAPTOR_H_
#define DLCSERVICE_DBUS_ADAPTORS_DBUS_ADAPTOR_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <imageloader/dbus-proxies.h>

#include "dlcservice/dbus_adaptors/org.chromium.DlcServiceInterface.h"
#include "dlcservice/dlc_service.h"
#include "dlcservice/types.h"

namespace dlcservice {

class DBusService : public org::chromium::DlcServiceInterfaceInterface {
 public:
  // Will take the ownership of |dlc_service|.
  explicit DBusService(DlcService* dlc_service);
  ~DBusService() = default;

  // org::chromium::DlServiceInterfaceInterface overrides:
  bool Install(brillo::ErrorPtr* err,
               const DlcModuleList& dlc_module_list_in) override;
  bool Uninstall(brillo::ErrorPtr* err, const std::string& id_in) override;
  bool GetInstalled(brillo::ErrorPtr* err,
                    DlcModuleList* dlc_module_list_out) override;

 private:
  DlcService* dlc_service_;

  DISALLOW_COPY_AND_ASSIGN(DBusService);
};

class DBusAdaptor : public org::chromium::DlcServiceInterfaceAdaptor,
                    public DlcService::Observer {
 public:
  // Will take the ownership of |dbus_service|.
  explicit DBusAdaptor(std::unique_ptr<DBusService> dbus_service);
  ~DBusAdaptor() = default;

  // |DlcService::Observer| override.
  void SendInstallStatus(const InstallStatus& status) override;

 private:
  std::unique_ptr<DBusService> dbus_service_;

  DISALLOW_COPY_AND_ASSIGN(DBusAdaptor);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DBUS_ADAPTORS_DBUS_ADAPTOR_H_
