// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/modem_info.h"

#include <memory>
#include <utility>

#include <ModemManager/ModemManager.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "shill/cellular/mock_dbus_objectmanager_proxy.h"
#include "shill/cellular/modem.h"
#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"

using testing::_;
using testing::SaveArg;
using testing::Test;

namespace shill {

namespace {
const RpcIdentifier kModemPath =
    RpcIdentifier("/org/freedesktop/ModemManager1/Modem/0");
}

class ModemInfoForTest : public ModemInfo {
 public:
  ModemInfoForTest(ControlInterface* control,
                   EventDispatcher* dispatcher,
                   Metrics* metrics,
                   Manager* manager)
      : ModemInfo(control, dispatcher, metrics, manager) {
    // See note for |mock_proxy_|.
    mock_proxy_ = std::make_unique<MockDBusObjectManagerProxy>();
    mock_proxy_->IgnoreSetCallbacks();
  }

  std::unique_ptr<DBusObjectManagerProxyInterface> CreateProxy() override {
    return std::move(mock_proxy_);
  }

  std::unique_ptr<Modem> CreateModem(
      const RpcIdentifier& path,
      const InterfaceToProperties& properties) override {
    return std::make_unique<Modem>(modemmanager::kModemManager1ServiceName,
                                   path, this);
  }

  MockDBusObjectManagerProxy* GetMockProxy() {
    CHECK(mock_proxy_);
    return mock_proxy_.get();
  }

 private:
  // Note: Ownership will be relenquished when CreateProxy() is called.
  std::unique_ptr<MockDBusObjectManagerProxy> mock_proxy_;
};

class ModemInfoTest : public Test {
 public:
  ModemInfoTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        modem_info_(&control_interface_, &dispatcher_, &metrics_, &manager_) {}

 protected:
  void Connect(const ObjectsWithProperties& expected_objects) {
    ManagedObjectsCallback get_managed_objects_callback;
    EXPECT_CALL(*modem_info_.GetMockProxy(), GetManagedObjects(_, _, _))
        .WillOnce(SaveArg<1>(&get_managed_objects_callback));

    modem_info_.Start();
    modem_info_.Connect();
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

  MockControl control_interface_;
  EventDispatcherForTest dispatcher_;
  MockMetrics metrics_;
  MockManager manager_;
  ModemInfoForTest modem_info_;
};

TEST_F(ModemInfoTest, ConnectDisconnect) {
  modem_info_.Start();
  EXPECT_FALSE(modem_info_.service_connected_);

  modem_info_.Connect();
  EXPECT_TRUE(modem_info_.service_connected_);
  EXPECT_EQ(0, modem_info_.modems_.size());

  modem_info_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_EQ(1, modem_info_.modems_.size());

  modem_info_.Disconnect();
  EXPECT_FALSE(modem_info_.service_connected_);
  EXPECT_EQ(0, modem_info_.modems_.size());
}

TEST_F(ModemInfoTest, AddRemoveModem) {
  modem_info_.Start();
  modem_info_.Connect();
  EXPECT_FALSE(modem_info_.ModemExists(kModemPath));

  // Remove non-existent modem path.
  modem_info_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_info_.ModemExists(kModemPath));

  modem_info_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_TRUE(modem_info_.ModemExists(kModemPath));

  // Add an already added modem.
  modem_info_.AddModem(kModemPath, InterfaceToProperties());
  EXPECT_TRUE(modem_info_.ModemExists(kModemPath));

  modem_info_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_info_.ModemExists(kModemPath));

  // Remove an already removed modem path.
  modem_info_.RemoveModem(kModemPath);
  EXPECT_FALSE(modem_info_.ModemExists(kModemPath));
}

TEST_F(ModemInfoTest, StartStop) {
  modem_info_.Start();
  EXPECT_NE(nullptr, modem_info_.proxy_);

  modem_info_.Stop();
  EXPECT_EQ(nullptr, modem_info_.proxy_);
}

TEST_F(ModemInfoTest, Connect) {
  Connect(GetModemWithProperties());
  EXPECT_EQ(1, modem_info_.modems_.size());
  EXPECT_TRUE(base::Contains(modem_info_.modems_, kModemPath));
}

TEST_F(ModemInfoTest, AddRemoveInterfaces) {
  // Have nothing come back from GetManagedObjects.
  Connect(ObjectsWithProperties());
  EXPECT_EQ(0, modem_info_.modems_.size());

  // Add an object that doesn't have a modem interface.  Nothing should be added
  modem_info_.OnInterfacesAddedSignal(kModemPath, InterfaceToProperties());
  EXPECT_EQ(0, modem_info_.modems_.size());

  // Actually add a modem
  modem_info_.OnInterfacesAddedSignal(kModemPath,
                                      GetModemWithProperties()[kModemPath]);
  EXPECT_EQ(1, modem_info_.modems_.size());

  // Remove an irrelevant interface
  modem_info_.OnInterfacesRemovedSignal(kModemPath, {"not.a.modem.interface"});
  EXPECT_EQ(1, modem_info_.modems_.size());

  // Remove the modem
  modem_info_.OnInterfacesRemovedSignal(kModemPath, {MM_DBUS_INTERFACE_MODEM});
  EXPECT_EQ(0, modem_info_.modems_.size());
}

}  // namespace shill
