// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_manager.h"

#include <memory>
#include <utility>

#include <base/stl_util.h>
#include <ModemManager/ModemManager.h>

#include "shill/cellular/modem.h"
#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"

namespace shill {

namespace {
constexpr int kGetManagedObjectsTimeout = 5000;
}

ModemManager::ModemManager(const std::string& service,
                           const RpcIdentifier& path,
                           ModemInfo* modem_info)
    : service_(service),
      path_(path),
      service_connected_(false),
      modem_info_(modem_info),
      weak_ptr_factory_(this) {}

ModemManager::~ModemManager() {
  Stop();
}

void ModemManager::Start() {
  LOG(INFO) << "Start watching modem manager service: " << service_;
  CHECK(!proxy_);
  proxy_ = CreateProxy();
}

void ModemManager::Stop() {
  LOG(INFO) << "Stop watching modem manager service: " << service_;
  proxy_.reset();
  Disconnect();
}

void ModemManager::OnDeviceInfoAvailable(const std::string& link_name) {
  for (const auto& modem_entry : modems_) {
    modem_entry.second->OnDeviceInfoAvailable(link_name);
  }
}

std::unique_ptr<DBusObjectManagerProxyInterface> ModemManager::CreateProxy() {
  std::unique_ptr<DBusObjectManagerProxyInterface> proxy =
      modem_info_->control_interface()->CreateDBusObjectManagerProxy(
          path_, service_,
          base::Bind(&ModemManager::OnAppeared, weak_ptr_factory_.GetWeakPtr()),
          base::Bind(&ModemManager::OnVanished,
                     weak_ptr_factory_.GetWeakPtr()));
  proxy->set_interfaces_added_callback(Bind(
      &ModemManager::OnInterfacesAddedSignal, weak_ptr_factory_.GetWeakPtr()));
  proxy->set_interfaces_removed_callback(
      Bind(&ModemManager::OnInterfacesRemovedSignal,
           weak_ptr_factory_.GetWeakPtr()));
  return proxy;
}

std::unique_ptr<Modem> ModemManager::CreateModem(
    const RpcIdentifier& path, const InterfaceToProperties& properties) {
  auto modem = std::make_unique<Modem>(service_, path, modem_info_);
  modem->CreateDeviceMM1(properties);
  return modem;
}

void ModemManager::Connect() {
  service_connected_ = true;
  Error error;
  CHECK(proxy_);
  proxy_->GetManagedObjects(&error,
                            Bind(&ModemManager::OnGetManagedObjectsReply,
                                 weak_ptr_factory_.GetWeakPtr()),
                            kGetManagedObjectsTimeout);
}

void ModemManager::Disconnect() {
  modems_.clear();
  service_connected_ = false;
}

void ModemManager::OnAppeared() {
  LOG(INFO) << "Modem manager " << service_ << " appeared.";
  Connect();
}

void ModemManager::OnVanished() {
  LOG(INFO) << "Modem manager " << service_ << " vanished.";
  Disconnect();
}

bool ModemManager::ModemExists(const RpcIdentifier& path) const {
  CHECK(service_connected_);
  return base::ContainsKey(modems_, path);
}

void ModemManager::AddModem(const RpcIdentifier& path,
                            const InterfaceToProperties& properties) {
  if (ModemExists(path)) {
    LOG(INFO) << "Modem " << path.value() << " already exists.";
    return;
  }
  std::unique_ptr<Modem> modem = CreateModem(path, properties);
  modems_[modem->path()] = std::move(modem);
}

void ModemManager::RemoveModem(const RpcIdentifier& path) {
  LOG(INFO) << "Remove modem: " << path.value();
  CHECK(service_connected_);
  modems_.erase(path);
}

void ModemManager::OnInterfacesAddedSignal(
    const RpcIdentifier& object_path, const InterfaceToProperties& properties) {
  if (!base::ContainsKey(properties, MM_DBUS_INTERFACE_MODEM)) {
    LOG(ERROR) << "Interfaces added, but not modem interface.";
    return;
  }
  AddModem(object_path, properties);
}

void ModemManager::OnInterfacesRemovedSignal(
    const RpcIdentifier& object_path,
    const std::vector<std::string>& interfaces) {
  LOG(INFO) << "MM1:  Removing interfaces from " << object_path.value();
  if (!base::ContainsValue(interfaces, MM_DBUS_INTERFACE_MODEM)) {
    // In theory, a modem could drop, say, 3GPP, but not CDMA.  In
    // practice, we don't expect this
    LOG(ERROR) << "Interfaces removed, but not modem interface";
    return;
  }
  RemoveModem(object_path);
}

void ModemManager::OnGetManagedObjectsReply(
    const ObjectsWithProperties& objects, const Error& error) {
  if (!error.IsSuccess())
    return;
  for (const auto& object_properties_pair : objects) {
    OnInterfacesAddedSignal(object_properties_pair.first,
                            object_properties_pair.second);
  }
}

}  // namespace shill
