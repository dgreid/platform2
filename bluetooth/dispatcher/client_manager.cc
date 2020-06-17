// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/client_manager.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/memory/ref_counted.h>
#include <base/stl_util.h>

namespace bluetooth {

ClientManager::ClientManager(
    scoped_refptr<dbus::Bus> bus,
    std::unique_ptr<DBusConnectionFactory> dbus_connection_factory)
    : bus_(bus),
      dbus_connection_factory_(std::move(dbus_connection_factory)),
      weak_ptr_factory_(this) {}

DispatcherClient* ClientManager::EnsureClientAdded(
    const std::string& client_address) {
  if (base::Contains(clients_, client_address))
    return clients_[client_address].get();

  VLOG(1) << "Adding new client " << client_address;
  auto client = std::make_unique<DispatcherClient>(
      bus_, client_address, dbus_connection_factory_.get());
  client->StartUpwardForwarding();
  client->GetDBusClient()->WatchClientUnavailable(
      base::Bind(&ClientManager::OnClientUnavailable,
                 weak_ptr_factory_.GetWeakPtr(), client_address));
  clients_[client_address] = std::move(client);
  return clients_[client_address].get();
}

void ClientManager::OnClientUnavailable(const std::string& client_address) {
  VLOG(1) << "Client " << client_address << " becomes unavailable";
  if (clients_.erase(client_address))
    VLOG(1) << "Removed client " << client_address;
}

}  // namespace bluetooth
