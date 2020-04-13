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

namespace {

const char kService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kPath = RpcIdentifier("/org/freedesktop/ModemManager1");
const RpcIdentifier kModemPath =
    RpcIdentifier("/org/freedesktop/ModemManager1/Modem/0");

}  // namespace

class ModemManagerForTest : public ModemManager {
 public:
  ModemManagerForTest(const string& service,
                      const RpcIdentifier& path,
                      ModemInfo* modem_info)
      : ModemManager(service, path, modem_info) {
    // See note for mock_proxy_
    mock_proxy_ = std::make_unique<MockDBusObjectManagerProxy>();
    mock_proxy_->IgnoreSetCallbacks();
  }

  std::unique_ptr<DBusObjectManagerProxyInterface> CreateProxy() override {
    return std::move(mock_proxy_);
  }

  std::unique_ptr<Modem> CreateModem(
      const RpcIdentifier& path,
      const InterfaceToProperties& properties) override {
    return std::make_unique<Modem>(service(), path, modem_info());
  }

  MockDBusObjectManagerProxy* GetMockProxy() {
    CHECK(mock_proxy_);
    return mock_proxy_.get();
  }

 private:
  // Note: Ownership will be relenquished when CreateProxy() is called.
  std::unique_ptr<MockDBusObjectManagerProxy> mock_proxy_;
};

class ModemManagerTest : public Test {
 public:
  ModemManagerTest()
      : manager_(&control_, &dispatcher_, nullptr),
        modem_info_(&control_, &dispatcher_, nullptr, &manager_),
        modem_manager_(kService, kPath, &modem_info_) {}

 protected:
  std::unique_ptr<StrictModem> CreateModem() {
    return std::make_unique<StrictModem>(kService, kModemPath, &modem_info_);
  }

  void Connect(const ObjectsWithProperties& expected_objects) {
    ManagedObjectsCallback get_managed_objects_callback;
    EXPECT_CALL(*modem_manager_.GetMockProxy(), GetManagedObjects(_, _, _))
        .WillOnce(SaveArg<1>(&get_managed_objects_callback));

    modem_manager_.Start();
    modem_manager_.Connect();
    get_managed_objects_callback.Run(expected_objects, Error());
  }

  ObjectsWithProperties GetModemWithProperties() {
    KeyValueStore o_fd_mm1_modem;

    InterfaceToProperties properties;
    properties[MM_DBUS_INTERFACE_MODEM] = o_fd_mm1_modem;

    ObjectsWithProperties objects_with_properties;
    objects_with_properties[kModemPath] = properties;

    return objects_with_properties;
  }

  EventDispatcherForTest dispatcher_;
  MockControl control_;
  MockManager manager_;
  MockModemInfo modem_info_;
  ModemManagerForTest modem_manager_;
};

TEST_F(ModemManagerTest, ConnectDisconnect) {
  modem_manager_.Start();
  EXPECT_FALSE(modem_manager_.service_connected_);

  modem_manager_.Connect();
  EXPECT_TRUE(modem_manager_.service_connected_);
  EXPECT_EQ(0, modem_manager_.modems_.size());

  modem_manager_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_EQ(1, modem_manager_.modems_.size());

  modem_manager_.Disconnect();
  EXPECT_FALSE(modem_manager_.service_connected_);
  EXPECT_EQ(0, modem_manager_.modems_.size());
}

TEST_F(ModemManagerTest, AddRemoveModem) {
  modem_manager_.Start();
  modem_manager_.Connect();
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  // Remove non-existent modem path.
  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  modem_manager_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_TRUE(modem_manager_.ModemExists(kModemPath));

  // Add an already added modem.
  modem_manager_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_TRUE(modem_manager_.ModemExists(kModemPath));

  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));

  // Remove an already removed modem path.
  modem_manager_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_manager_.ModemExists(kModemPath));
}

TEST_F(ModemManagerTest, StartStop) {
  modem_manager_.Start();
  EXPECT_NE(nullptr, modem_manager_.proxy_);

  modem_manager_.Stop();
  EXPECT_EQ(nullptr, modem_manager_.proxy_);
}

TEST_F(ModemManagerTest, Connect) {
  Connect(GetModemWithProperties());
  EXPECT_EQ(1, modem_manager_.modems_.size());
  EXPECT_TRUE(base::ContainsKey(modem_manager_.modems_, kModemPath));
}

TEST_F(ModemManagerTest, AddRemoveInterfaces) {
  // Have nothing come back from GetManagedObjects
  Connect(ObjectsWithProperties());
  EXPECT_EQ(0, modem_manager_.modems_.size());

  // Add an object that doesn't have a modem interface.  Nothing should be added
  modem_manager_.OnInterfacesAddedSignal(kModemPath, InterfaceToProperties());
  EXPECT_EQ(0, modem_manager_.modems_.size());

  // Actually add a modem
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
