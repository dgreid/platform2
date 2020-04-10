// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_manager.h"

#include <ModemManager/ModemManager.h>

#include <memory>
#include <utility>

#include <base/stl_util.h>

#include "shill/cellular/mock_dbus_objectmanager_proxy.h"
#include "shill/cellular/mock_modem.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using std::string;
using testing::_;
using testing::ByMove;
using testing::Return;
using testing::SaveArg;
using testing::Test;

namespace shill {

class ModemManagerTest : public Test {
 public:
  ModemManagerTest()
      : manager_(&control_, &dispatcher_, nullptr),
        modem_info_(&control_, &dispatcher_, nullptr, &manager_) {}

 protected:
  static const char kService[];
  static const RpcIdentifier kPath;
  static const RpcIdentifier kModemPath;

  std::unique_ptr<StrictModem> CreateModem() {
    return std::make_unique<StrictModem>(kService, kModemPath, &modem_info_);
  }

  EventDispatcherForTest dispatcher_;
  MockControl control_;
  MockManager manager_;
  MockModemInfo modem_info_;
};

const char ModemManagerTest::kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier ModemManagerTest::kPath =
    RpcIdentifier("/org/freedesktop/ModemManager1");
const RpcIdentifier ModemManagerTest::kModemPath =
    RpcIdentifier("/org/freedesktop/ModemManager1/Modem/0");

class ModemManagerForTest : public ModemManager {
 public:
  ModemManagerForTest(const string& service,
                      const RpcIdentifier& path,
                      ModemInfo* modem_info)
      : ModemManager(service, path, modem_info) {}

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void,
              InitModem,
              (Modem*, const InterfaceToProperties&),
              (override));
};

class ModemManagerCoreTest : public ModemManagerTest {
 public:
  ModemManagerCoreTest() : modem_manager_(kService, kPath, &modem_info_) {}

 protected:
  ModemManagerForTest modem_manager_;
};

TEST_F(ModemManagerCoreTest, ConnectDisconnect) {
  EXPECT_FALSE(modem_manager_.service_connected_);
  modem_manager_.Connect();
  EXPECT_TRUE(modem_manager_.service_connected_);

  modem_manager_.RecordAddedModem(CreateModem());
  EXPECT_EQ(1, modem_manager_.modems_.size());

  modem_manager_.Disconnect();
  EXPECT_EQ(0, modem_manager_.modems_.size());
  EXPECT_FALSE(modem_manager_.service_connected_);
}

TEST_F(ModemManagerCoreTest, AddRemoveModem) {
  modem_manager_.Connect();
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  // Remove non-existent modem path.
  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  modem_manager_.RecordAddedModem(CreateModem());
  EXPECT_TRUE(modem_manager_.ModemExists(kModemPath));

  // Add an already added modem.
  modem_manager_.RecordAddedModem(CreateModem());
  EXPECT_TRUE(modem_manager_.ModemExists(kModemPath));

  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  // Remove an already removed modem path.
  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));
}

class ModemManager1MockInit : public ModemManager {
 public:
  ModemManager1MockInit(const string& service,
                        const RpcIdentifier& path,
                        ModemInfo* modem_info_)
      : ModemManager(service, path, modem_info_) {}

  MOCK_METHOD(void,
              InitModem,
              (Modem*, const InterfaceToProperties&),
              (override));
};

class ModemManager1Test : public ModemManagerTest {
 public:
  ModemManager1Test() : modem_manager_(kService, kPath, &modem_info_) {}

 protected:
  std::unique_ptr<MockDBusObjectManagerProxy> CreateDBusObjectManagerProxy() {
    auto proxy = std::make_unique<MockDBusObjectManagerProxy>();
    proxy->IgnoreSetCallbacks();
    return proxy;
  }

  void Connect(const ObjectsWithProperties& expected_objects) {
    auto proxy = CreateDBusObjectManagerProxy();

    ManagedObjectsCallback get_managed_objects_callback;
    EXPECT_CALL(*proxy, GetManagedObjects(_, _, _))
        .WillOnce(SaveArg<1>(&get_managed_objects_callback));

    // Set up proxy.
    modem_manager_.proxy_ = std::move(proxy);
    modem_manager_.Connect();
    get_managed_objects_callback.Run(expected_objects, Error());
  }

  static ObjectsWithProperties GetModemWithProperties() {
    KeyValueStore o_fd_mm1_modem;

    InterfaceToProperties properties;
    properties[MM_DBUS_INTERFACE_MODEM] = o_fd_mm1_modem;

    ObjectsWithProperties objects_with_properties;
    objects_with_properties[kModemPath] = properties;

    return objects_with_properties;
  }

  ModemManager1MockInit modem_manager_;
};

TEST_F(ModemManager1Test, StartStop) {
  EXPECT_EQ(nullptr, modem_manager_.proxy_);

  auto proxy = CreateDBusObjectManagerProxy();
  EXPECT_CALL(*proxy, set_interfaces_added_callback(_));
  EXPECT_CALL(*proxy, set_interfaces_removed_callback(_));
  EXPECT_CALL(control_, CreateDBusObjectManagerProxy(kPath, kService, _, _))
      .WillOnce(Return(ByMove(std::move(proxy))));

  modem_manager_.Start();
  EXPECT_NE(nullptr, modem_manager_.proxy_);

  modem_manager_.Stop();
  EXPECT_EQ(nullptr, modem_manager_.proxy_);
}

TEST_F(ModemManager1Test, Connect) {
  Connect(GetModemWithProperties());
  EXPECT_EQ(1, modem_manager_.modems_.size());
  EXPECT_TRUE(base::ContainsKey(modem_manager_.modems_, kModemPath));
}

TEST_F(ModemManager1Test, AddRemoveInterfaces) {
  // Have nothing come back from GetManagedObjects
  Connect(ObjectsWithProperties());
  EXPECT_EQ(0, modem_manager_.modems_.size());

  // Add an object that doesn't have a modem interface.  Nothing should be added
  EXPECT_CALL(modem_manager_, InitModem(_, _)).Times(0);
  modem_manager_.OnInterfacesAddedSignal(kModemPath, InterfaceToProperties());
  EXPECT_EQ(0, modem_manager_.modems_.size());

  // Actually add a modem
  EXPECT_CALL(modem_manager_, InitModem(_, _)).Times(1);
  modem_manager_.OnInterfacesAddedSignal(kModemPath,
                                         GetModemWithProperties()[kModemPath]);
  EXPECT_EQ(1, modem_manager_.modems_.size());

  // Remove an irrelevant interface
  modem_manager_.OnInterfacesRemovedSignal(kModemPath,
                                           {"not.a.modem.interface"});
  EXPECT_EQ(1, modem_manager_.modems_.size());

  // Remove the modem
  modem_manager_.OnInterfacesRemovedSignal(kModemPath,
                                           {MM_DBUS_INTERFACE_MODEM});
  EXPECT_EQ(0, modem_manager_.modems_.size());
}

}  // namespace shill
