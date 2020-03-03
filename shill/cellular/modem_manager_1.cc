// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_manager.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/stl_util.h>
#include <ModemManager/ModemManager.h>

#include "shill/cellular/modem.h"
#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/logging.h"

using base::Bind;
using std::string;
using std::vector;

namespace shill {

ModemManager1::ModemManager1(const string& service,
                             const RpcIdentifier& path,
                             ModemInfo* modem_info)
    : ModemManager(service, path, modem_info), weak_ptr_factory_(this) {}

ModemManager1::~ModemManager1() {
  Stop();
}

void ModemManager1::Start() {
  LOG(INFO) << "Start watching modem manager service: " << service();
  CHECK(!proxy_);
  proxy_ = control_interface()->CreateDBusObjectManagerProxy(
      path(), service(),
      base::Bind(&ModemManager1::OnAppeared, weak_ptr_factory_.GetWeakPtr()),
      base::Bind(&ModemManager1::OnVanished, weak_ptr_factory_.GetWeakPtr()));
  proxy_->set_interfaces_added_callback(Bind(
      &ModemManager1::OnInterfacesAddedSignal, weak_ptr_factory_.GetWeakPtr()));
  proxy_->set_interfaces_removed_callback(
      Bind(&ModemManager1::OnInterfacesRemovedSignal,
           weak_ptr_factory_.GetWeakPtr()));
}

void ModemManager1::Stop() {
  LOG(INFO) << "Stop watching modem manager service: " << service();
  proxy_.reset();
  Disconnect();
}

void ModemManager1::Connect() {
  ModemManager::Connect();
  // TODO(rochberg):  Make global kDBusDefaultTimeout and use it here
  Error error;
  proxy_->GetManagedObjects(&error,
                            Bind(&ModemManager1::OnGetManagedObjectsReply,
                                 weak_ptr_factory_.GetWeakPtr()),
                            5000);
}

void ModemManager1::Disconnect() {
  ModemManager::Disconnect();
}

void ModemManager1::AddModem1(const RpcIdentifier& path,
                              const InterfaceToProperties& properties) {
  if (ModemExists(path)) {
    LOG(INFO) << "Modem " << path.value() << " already exists.";
    return;
  }

  auto modem = std::make_unique<Modem1>(service(), path, modem_info());
  InitModem1(modem.get(), properties);

  RecordAddedModem(std::move(modem));
}

void ModemManager1::InitModem1(Modem1* modem,
                               const InterfaceToProperties& properties) {
  modem->CreateDeviceMM1(properties);
}

// signal methods
// Also called by OnGetManagedObjectsReply
void ModemManager1::OnInterfacesAddedSignal(
    const RpcIdentifier& object_path, const InterfaceToProperties& properties) {
  if (base::ContainsKey(properties, MM_DBUS_INTERFACE_MODEM)) {
    AddModem1(object_path, properties);
  } else {
    LOG(ERROR) << "Interfaces added, but not modem interface.";
  }
}

void ModemManager1::OnInterfacesRemovedSignal(
    const RpcIdentifier& object_path, const vector<string>& interfaces) {
  LOG(INFO) << "MM1:  Removing interfaces from " << object_path.value();
  if (base::ContainsValue(interfaces, MM_DBUS_INTERFACE_MODEM)) {
    RemoveModem(object_path);
  } else {
    // In theory, a modem could drop, say, 3GPP, but not CDMA.  In
    // practice, we don't expect this
    LOG(ERROR) << "Interfaces removed, but not modem interface";
  }
}

// DBusObjectManagerProxy async method call
void ModemManager1::OnGetManagedObjectsReply(
    const ObjectsWithProperties& objects, const Error& error) {
  if (error.IsSuccess()) {
    for (const auto& object_properties_pair : objects) {
      OnInterfacesAddedSignal(object_properties_pair.first,
                              object_properties_pair.second);
    }
  }
}

}  // namespace shill
