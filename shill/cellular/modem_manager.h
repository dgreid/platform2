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
  virtual void Start() = 0;

  // Stops watching for the DBus modem manager service and destroys any
  // associated modems.
  virtual void Stop() = 0;

  void OnDeviceInfoAvailable(const std::string& link_name);

 protected:
  const std::string& service() const { return service_; }
  const RpcIdentifier& path() const { return path_; }
  ControlInterface* control_interface() const {
    return modem_info_->control_interface();
  }
  ModemInfo* modem_info() const { return modem_info_; }

  // Service availability callbacks.
  void OnAppeared();
  void OnVanished();

  // Connect/Disconnect to a modem manager service.
  // Inheriting classes must call this superclass method.
  virtual void Connect();
  // Inheriting classes must call this superclass method.
  virtual void Disconnect();

  bool ModemExists(const RpcIdentifier& path) const;
  // Put the modem into our modem map
  void RecordAddedModem(std::unique_ptr<Modem> modem);

  // Removes a modem on |path|.
  void RemoveModem(const RpcIdentifier& path);

 private:
  friend class ModemManagerCoreTest;
  friend class ModemManager1Test;

  FRIEND_TEST(ModemManager1Test, AddRemoveInterfaces);
  FRIEND_TEST(ModemManager1Test, Connect);
  FRIEND_TEST(ModemManagerCoreTest, AddRemoveModem);
  FRIEND_TEST(ModemManagerCoreTest, ConnectDisconnect);

  const std::string service_;
  const RpcIdentifier path_;
  bool service_connected_;

  // Maps a modem path to a modem instance.
  std::map<RpcIdentifier, std::unique_ptr<Modem>> modems_;

  ModemInfo* modem_info_;

  DISALLOW_COPY_AND_ASSIGN(ModemManager);
};

class ModemManager1 : public ModemManager {
 public:
  ModemManager1(const std::string& service,
                const RpcIdentifier& path,
                ModemInfo* modem_info);
  ~ModemManager1() override;

  void Start() override;
  void Stop() override;

 protected:
  void AddModem(const RpcIdentifier& path,
                const InterfaceToProperties& properties);
  virtual void InitModem(Modem* modem, const InterfaceToProperties& properties);

  // ModemManager methods
  void Connect() override;
  void Disconnect() override;

  // DBusObjectManagerProxyDelegate signal methods
  virtual void OnInterfacesAddedSignal(const RpcIdentifier& object_path,
                                       const InterfaceToProperties& properties);
  virtual void OnInterfacesRemovedSignal(
      const RpcIdentifier& object_path,
      const std::vector<std::string>& interfaces);

  // DBusObjectManagerProxyDelegate method callbacks
  virtual void OnGetManagedObjectsReply(
      const ObjectsWithProperties& objects_with_properties, const Error& error);

 private:
  friend class ModemManager1Test;
  FRIEND_TEST(ModemManager1Test, Connect);
  FRIEND_TEST(ModemManager1Test, AddRemoveInterfaces);
  FRIEND_TEST(ModemManager1Test, StartStop);

  std::unique_ptr<DBusObjectManagerProxyInterface> proxy_;
  base::WeakPtrFactory<ModemManager1> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ModemManager1);
};

}  // namespace shill

#endif  // SHILL_CELLULAR_MODEM_MANAGER_H_
