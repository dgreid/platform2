// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <brillo/dbus/mock_exported_object_manager.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_manager.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "bluetooth/dispatcher/dispatcher.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

namespace bluetooth {

class DispatcherTest : public ::testing::Test {
 public:
  void SetUp() override {
    bus_ = new dbus::MockBus(dbus::Bus::Options());
    EXPECT_CALL(*bus_, AssertOnOriginThread()).Times(AnyNumber());
#if BASE_VER < 679961
    EXPECT_CALL(*bus_, GetDBusTaskRunner())
        .WillOnce(Return(task_environment_.GetMainThreadTaskRunner().get()));
#endif
    EXPECT_CALL(*bus_, AssertOnDBusThread()).Times(AnyNumber());
    EXPECT_CALL(*bus_, Connect()).WillRepeatedly(Return(false));

    dbus::ObjectPath object_manager_path(
        bluez_object_manager::kBluezObjectManagerServicePath);
    object_manager_object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), bluez_object_manager::kBluezObjectManagerServiceName,
        object_manager_path);
    EXPECT_CALL(*bus_, GetObjectProxy(
                           bluez_object_manager::kBluezObjectManagerServiceName,
                           object_manager_path))
        .WillOnce(Return(object_manager_object_proxy_.get()));
    source_object_manager_ = new dbus::MockObjectManager(
        bus_.get(), bluez_object_manager::kBluezObjectManagerServiceName,
        object_manager_path);

    auto exported_object_manager =
        std::make_unique<brillo::dbus_utils::MockExportedObjectManager>(
            bus_, dbus::ObjectPath(
                      bluez_object_manager::kBluezObjectManagerServicePath));

    exported_object_manager_wrapper_ =
        std::make_unique<ExportedObjectManagerWrapper>(
            bus_, std::move(exported_object_manager));

#if BASE_VER < 679961
    // Force MessageLoop to run pending tasks as effect of instantiating
    // MockObjectManager. Needed to avoid memory leaks because pending tasks
    // are unowned pointers that will only self destruct after being run.
    base::RunLoop().RunUntilIdle();
#endif
    dispatcher_ = std::make_unique<Dispatcher>(
        bus_, exported_object_manager_wrapper_.get());
  }

 protected:
  void TestPassthrough(PassthroughMode passthrough_mode,
                       const std::vector<std::string>& service_names) {
    dbus::ObjectPath root_path(
        bluetooth_object_manager::kBluetoothObjectManagerServicePath);

    for (const std::string& service_name : service_names) {
      EXPECT_CALL(*bus_, GetObjectManager(service_name, root_path))
          .WillRepeatedly(Return(source_object_manager_.get()));
    }

    std::vector<std::string> bluez_interfaces = {
        bluetooth_adapter::kBluetoothAdapterInterface,
        bluetooth_device::kBluetoothDeviceInterface,
        bluetooth_gatt_characteristic::kBluetoothGattCharacteristicInterface,
        bluetooth_gatt_manager::kBluetoothGattManagerInterface,
        bluetooth_input::kBluetoothInputInterface,
        bluetooth_media::kBluetoothMediaInterface,
        bluetooth_gatt_service::kBluetoothGattServiceInterface,
        bluetooth_advertising_manager::kBluetoothAdvertisingManagerInterface,
        bluetooth_gatt_descriptor::kBluetoothGattDescriptorInterface,
        bluetooth_media_transport::kBluetoothMediaTransportInterface,
        bluetooth_agent_manager::kBluetoothAgentManagerInterface,
        bluetooth_profile_manager::kBluetoothProfileManagerInterface,
        bluetooth_plugin_device::kBluetoothPluginInterface,
    };

    // Should listen to BlueZ interfaces.
    for (const std::string& interface_name : bluez_interfaces)
      EXPECT_CALL(*source_object_manager_, RegisterInterface(interface_name, _))
          .Times(service_names.size());
    dispatcher_->Init(passthrough_mode);

    // Free up all resources.
    for (const std::string& interface_name : bluez_interfaces)
      EXPECT_CALL(*source_object_manager_, UnregisterInterface(interface_name))
          .Times(service_names.size());
    dispatcher_->Shutdown();
  }

#if BASE_VER < 679961
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
#endif
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> object_manager_object_proxy_;
  scoped_refptr<dbus::MockObjectManager> source_object_manager_;
  std::unique_ptr<ExportedObjectManagerWrapper>
      exported_object_manager_wrapper_;
  std::unique_ptr<Dispatcher> dispatcher_;
};

TEST_F(DispatcherTest, NoPassthrough) {
  // No passthrough fallbacks to BlueZ passthrough.
  TestPassthrough(PassthroughMode::MULTIPLEX,
                  {bluez_object_manager::kBluezObjectManagerServiceName,
                   newblue_object_manager::kNewblueObjectManagerServiceName});
}

TEST_F(DispatcherTest, PassthroughBluez) {
  TestPassthrough(PassthroughMode::BLUEZ_ONLY,
                  {bluez_object_manager::kBluezObjectManagerServiceName});
}

TEST_F(DispatcherTest, PassthroughNewblue) {
  TestPassthrough(PassthroughMode::NEWBLUE_ONLY,
                  {newblue_object_manager::kNewblueObjectManagerServiceName});
}

}  // namespace bluetooth
