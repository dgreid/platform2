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
  // virtual for test mocks. TODO(crbug.com/984627): Use fakes or test setters.
  virtual void Start();

  // Stops watching for the DBus modem manager service and destroys any
  // associated modems.
  // virtual for test mocks. TODO(crbug.com/984627): Use fakes or test setters.
  virtual void Stop();

  void OnDeviceInfoAvailable(const std::string& link_name);

 private:
  friend class ModemManager1Test;
  friend class ModemManagerCoreTest;

  FRIEND_TEST(ModemManager1Test, AddRemoveInterfaces);
  FRIEND_TEST(ModemManager1Test, Connect);
  FRIEND_TEST(ModemManager1Test, StartStop);
  FRIEND_TEST(ModemManagerCoreTest, AddRemoveModem);
  FRIEND_TEST(ModemManagerCoreTest, ConnectDisconnect);

  // Connect/Disconnect to a modem manager service.
  void Connect();
  void Disconnect();

  // Service availability callbacks.
  void OnAppeared();
  void OnVanished();

  bool ModemExists(const RpcIdentifier& path) const;

  void AddModem(const RpcIdentifier& path,
                const InterfaceToProperties& properties);
  void RecordAddedModem(std::unique_ptr<Modem> modem);
  // virtual for test mocks. TODO(crbug.com/984627): Use fakes or test setters.
  virtual void InitModem(Modem* modem, const InterfaceToProperties& properties);
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
