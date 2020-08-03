// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/newblued/stack_sync_monitor.h"

#include <memory>

#include <base/bind.h>
#include <base/memory/ref_counted.h>
#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_manager.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/property.h>
#include <gtest/gtest.h>

namespace bluetooth {

using ::testing::Return;

class StackSyncMonitorTest : public ::testing::Test {
 public:
  StackSyncMonitorTest()
      : stack_sync_quitting_(false),
        bus_(new dbus::MockBus(dbus::Bus::Options())) {
    dbus::ObjectPath object_path(
        bluez_object_manager::kBluezObjectManagerServicePath);
    EXPECT_CALL(*bus_, GetDBusTaskRunner())
        .WillRepeatedly(
            Return(task_environment_.GetMainThreadTaskRunner().get()));
    object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), bluez_object_manager::kBluezObjectManagerServiceName,
        object_path);
    EXPECT_CALL(*bus_, GetObjectProxy(
                           bluez_object_manager::kBluezObjectManagerServiceName,
                           object_path))
        .WillRepeatedly(Return(object_proxy_.get()));
    object_manager_ = new dbus::MockObjectManager(
        bus_.get(), bluez_object_manager::kBluezObjectManagerServiceName,
        object_path);
    EXPECT_CALL(*bus_, GetObjectManager(
                           bluez_object_manager::kBluezObjectManagerServiceName,
                           object_path))
        .WillRepeatedly(Return(object_manager_.get()));
    properties_.reset(monitor_.CreateProperties(
        object_proxy_.get(), object_path,
        bluetooth_adapter::kBluetoothAdapterInterface));
  }

  void OnBluezDown() { stack_sync_quitting_ = true; }

 protected:
  bool stack_sync_quitting_;
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> object_proxy_;
  scoped_refptr<dbus::MockObjectManager> object_manager_;
  std::unique_ptr<dbus::PropertySet> properties_;
  StackSyncMonitor monitor_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
};

TEST_F(StackSyncMonitorTest, PowerDown) {
  monitor_.RegisterBluezDownCallback(
      bus_.get(),
      base::Bind(&StackSyncMonitorTest::OnBluezDown, base::Unretained(this)));

  // BlueZ adapter power on.
  monitor_.bluez_powered_.ReplaceValue(true);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stack_sync_quitting_);

  // BlueZ adapter power down, call BluezDownCallback.
  monitor_.bluez_powered_.ReplaceValue(false);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(stack_sync_quitting_);
}

TEST_F(StackSyncMonitorTest, BluezStackSyncQuitting) {
  monitor_.RegisterBluezDownCallback(
      bus_.get(),
      base::Bind(&StackSyncMonitorTest::OnBluezDown, base::Unretained(this)));

  // BlueZ adapter power on.
  monitor_.bluez_powered_.ReplaceValue(true);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);

  // BlueZ stack sync quitting, refrain from calling BluezDownCallback.
  monitor_.bluez_stack_sync_quitting_.ReplaceValue(true);
  properties_->NotifyPropertyChanged(
      bluetooth_adapter::kStackSyncQuittingProperty);
  monitor_.bluez_powered_.ReplaceValue(false);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stack_sync_quitting_);
}

TEST_F(StackSyncMonitorTest, NoCallback) {
  monitor_.RegisterBluezDownCallback(bus_.get(), base::Closure());

  // BlueZ adapter power on.
  monitor_.bluez_powered_.ReplaceValue(true);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);

  // BlueZ adapter power down, but no callback is provided.
  monitor_.bluez_powered_.ReplaceValue(false);
  properties_->NotifyPropertyChanged(bluetooth_adapter::kPoweredProperty);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(stack_sync_quitting_);
}

}  // namespace bluetooth
