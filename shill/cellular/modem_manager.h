// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MODEM_MANAGER_H_
#define SHILL_CELLULAR_MODEM_MANAGER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular/dbus_objectmanager_proxy_interface.h"
#include "shill/cellular/modem_info.h"
#include "shill/dbus_properties_proxy_interface.h"

namespace shill {

class ControlInterface;
class DBusObjectManagerProxyInterface;
class Modem;

// Handles a modem manager service and creates and destroys modem instances.
class ModemManager {
 public:
  ModemManager(const std::string& service,
               const RpcIdentifier& path,
               ModemInfo* modem_info);
  virtual ~ModemManager();

  // Starts watching for and handling the DBus modem manager service.
  void Start();

  // Stops watching for the DBus modem manager service and destroys any
  // associated modems.
  void Stop();

  void OnDeviceInfoAvailable(const std::string& link_name);

 protected:
  // The following methods are virtual to support test overrides.
  virtual std::unique_ptr<DBusObjectManagerProxyInterface> CreateProxy();
  virtual std::unique_ptr<Modem> CreateModem(
      const RpcIdentifier& path, const InterfaceToProperties& properties);

  ModemInfo* modem_info() { return modem_info_; }
  const std::string& service() const { return service_; }

 private:
  friend class ModemManagerTest;

  FRIEND_TEST(ModemManagerTest, AddRemoveModem);
  FRIEND_TEST(ModemManagerTest, ConnectDisconnect);
  FRIEND_TEST(ModemManagerTest, AddRemoveInterfaces);
  FRIEND_TEST(ModemManagerTest, Connect);
  FRIEND_TEST(ModemManagerTest, StartStop);

  // Connect/Disconnect to a modem manager service.
  void Connect();
  void Disconnect();

  // Service availability callbacks.
  void OnAppeared();
  void OnVanished();

  bool ModemExists(const RpcIdentifier& path) const;

  void AddModem(const RpcIdentifier& path,
                const InterfaceToProperties& properties);
  void RemoveModem(const RpcIdentifier& path);

  // DBusObjectManagerProxyDelegate signal methods
  void OnInterfacesAddedSignal(const RpcIdentifier& object_path,
                               const InterfaceToProperties& properties);
  void OnInterfacesRemovedSignal(const RpcIdentifier& object_path,
                                 const std::vector<std::string>& interfaces);

  // DBusObjectManagerProxyDelegate method callbacks
  void OnGetManagedObjectsReply(
      const ObjectsWithProperties& objects_with_properties, const Error& error);

  const std::string service_;
  const RpcIdentifier path_;
  bool service_connected_;

  // Maps a modem path to a modem instance.
  std::map<RpcIdentifier, std::unique_ptr<Modem>> modems_;

  ModemInfo* modem_info_;

  std::unique_ptr<DBusObjectManagerProxyInterface> proxy_;
  base::WeakPtrFactory<ModemManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ModemManager);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MODEM_MANAGER_H_
