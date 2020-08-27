// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/peripheral_battery_watcher.h"

#include <string>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "power_manager/common/test_main_loop_runner.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/proto_bindings/peripheral_battery_status.pb.h"

namespace power_manager {
namespace system {

using std::string;

namespace {

// Abort if it an expected battery update hasn't been received after this long.
constexpr base::TimeDelta kUpdateTimeout = base::TimeDelta::FromSeconds(3);

// Shorter update timeout to use when failure is expected.
constexpr base::TimeDelta kShortUpdateTimeout =
    base::TimeDelta::FromMilliseconds(100);

const char kDeviceModelName[] = "Test HID Mouse";

constexpr char kPeripheralBatterySysname[] = "hid-someperipheral-battery";
constexpr char kBluetoothBatterySysname[] = "hid-11:22:33:aa:bb:cc-battery";
constexpr char kNonPeripheralBatterySysname[] = "AC";

class TestWrapper : public DBusWrapperStub {
 public:
  TestWrapper() {}
  ~TestWrapper() override {}

  // Runs |loop_| until battery status is sent through D-Bus.
  bool RunUntilSignalSent(const base::TimeDelta& timeout) {
    return loop_runner_.StartLoop(timeout);
  }

  void EmitBareSignal(const std::string& signal_name) override {
    DBusWrapperStub::EmitBareSignal(signal_name);
    loop_runner_.StopLoop();
  }

  void EmitSignalWithProtocolBuffer(
      const std::string& signal_name,
      const google::protobuf::MessageLite& protobuf) override {
    DBusWrapperStub::EmitSignalWithProtocolBuffer(signal_name, protobuf);
    loop_runner_.StopLoop();
  }

 private:
  TestMainLoopRunner loop_runner_;

  DISALLOW_COPY_AND_ASSIGN(TestWrapper);
};

}  // namespace

class PeripheralBatteryWatcherTest : public ::testing::Test {
 public:
  PeripheralBatteryWatcherTest() {}
  ~PeripheralBatteryWatcherTest() override {}

  void SetUp() override {
    CHECK(temp_dir_.CreateUniqueTempDir());

    // Create a fake peripheral directory.
    base::FilePath device_dir =
        temp_dir_.GetPath().Append(kPeripheralBatterySysname);
    CHECK(base::CreateDirectory(device_dir));
    scope_file_ = device_dir.Append(PeripheralBatteryWatcher::kScopeFile);
    WriteFile(scope_file_, PeripheralBatteryWatcher::kScopeValueDevice);
    status_file_ = device_dir.Append(PeripheralBatteryWatcher::kStatusFile);
    model_name_file_ =
        device_dir.Append(PeripheralBatteryWatcher::kModelNameFile);
    WriteFile(model_name_file_, kDeviceModelName);
    peripheral_capacity_file_ =
        device_dir.Append(PeripheralBatteryWatcher::kCapacityFile);

    // Create a fake Bluetooth directory (distinguished by the name)
    device_dir = temp_dir_.GetPath().Append(kBluetoothBatterySysname);
    CHECK(base::CreateDirectory(device_dir));
    WriteFile(device_dir.Append(PeripheralBatteryWatcher::kScopeFile),
              PeripheralBatteryWatcher::kScopeValueDevice);
    WriteFile(device_dir.Append(PeripheralBatteryWatcher::kModelNameFile),
              kDeviceModelName);
    bluetooth_capacity_file_ =
        device_dir.Append(PeripheralBatteryWatcher::kCapacityFile);

    battery_.set_battery_path_for_testing(temp_dir_.GetPath());

    // Create a fake non-peripheral directory (there is no "scope" file.)
    device_dir = temp_dir_.GetPath().Append(kNonPeripheralBatterySysname);
    CHECK(base::CreateDirectory(device_dir));
    WriteFile(device_dir.Append(PeripheralBatteryWatcher::kModelNameFile),
              kDeviceModelName);
    non_peripheral_capacity_file_ =
        device_dir.Append(PeripheralBatteryWatcher::kCapacityFile);

    battery_.set_battery_path_for_testing(temp_dir_.GetPath());
  }

 protected:
  void WriteFile(const base::FilePath& path, const string& str) {
    ASSERT_EQ(str.size(), base::WriteFile(path, str.data(), str.size()));
  }

  // Temporary directory mimicking a /sys directory containing a set of sensor
  // devices.
  base::ScopedTempDir temp_dir_;

  base::FilePath scope_file_;
  base::FilePath status_file_;
  base::FilePath peripheral_capacity_file_;
  base::FilePath model_name_file_;
  base::FilePath non_peripheral_capacity_file_;
  base::FilePath bluetooth_capacity_file_;

  TestWrapper test_wrapper_;

  UdevStub udev_;

  PeripheralBatteryWatcher battery_;

 private:
  DISALLOW_COPY_AND_ASSIGN(PeripheralBatteryWatcherTest);
};

TEST_F(PeripheralBatteryWatcherTest, Basic) {
  std::string level = base::NumberToString(80);
  WriteFile(peripheral_capacity_file_, level);
  battery_.Init(&test_wrapper_, &udev_);
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));

  EXPECT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(80, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());
}

TEST_F(PeripheralBatteryWatcherTest, NoLevelReading) {
  battery_.Init(&test_wrapper_, &udev_);
  // Without writing battery level to the peripheral_capacity_file_, the loop
  // will timeout.
  EXPECT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));
}

TEST_F(PeripheralBatteryWatcherTest, SkipUnknownStatus) {
  // Batteries with unknown statuses should be skipped: http://b/64397082
  WriteFile(peripheral_capacity_file_, base::NumberToString(0));
  WriteFile(status_file_, PeripheralBatteryWatcher::kStatusValueUnknown);
  battery_.Init(&test_wrapper_, &udev_);
  ASSERT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));
}

TEST_F(PeripheralBatteryWatcherTest, AllowOtherStatus) {
  // Batteries with other statuses should be reported.
  WriteFile(peripheral_capacity_file_, base::NumberToString(20));
  WriteFile(status_file_, "Discharging");
  battery_.Init(&test_wrapper_, &udev_);
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));

  EXPECT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(20, proto.level());
}

TEST_F(PeripheralBatteryWatcherTest, UdevEvents) {
  // Initial reading of battery statuses.
  WriteFile(peripheral_capacity_file_, base::NumberToString(80));
  battery_.Init(&test_wrapper_, &udev_);
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));

  EXPECT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(80, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());

  // An udev ADD event appear for a peripheral device.
  WriteFile(peripheral_capacity_file_, base::NumberToString(70));
  udev_.NotifySubsystemObservers({{PeripheralBatteryWatcher::kUdevSubsystem, "",
                                   kPeripheralBatterySysname, ""},
                                  UdevEvent::Action::ADD});
  // Check that powerd reads the battery information and sends an update signal.
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));
  ASSERT_EQ(2, test_wrapper_.num_sent_signals());
  EXPECT_TRUE(test_wrapper_.GetSentSignal(1, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(70, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());

  // An udev CHANGE event appear for a peripheral device.
  WriteFile(peripheral_capacity_file_, base::NumberToString(60));
  udev_.NotifySubsystemObservers({{PeripheralBatteryWatcher::kUdevSubsystem, "",
                                   kPeripheralBatterySysname, ""},
                                  UdevEvent::Action::CHANGE});
  // Check that powerd reads the battery information and sends an update signal.
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));
  ASSERT_EQ(3, test_wrapper_.num_sent_signals());
  EXPECT_TRUE(test_wrapper_.GetSentSignal(2, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(60, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());

  // An udev REMOVE event appear for a peripheral device.
  WriteFile(peripheral_capacity_file_, base::NumberToString(60));
  udev_.NotifySubsystemObservers({{PeripheralBatteryWatcher::kUdevSubsystem, "",
                                   kPeripheralBatterySysname, ""},
                                  UdevEvent::Action::REMOVE});
  // A REMOVE event should not trigger battery update signal.
  EXPECT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));
}

TEST_F(PeripheralBatteryWatcherTest, NonPeripheralUdevEvents) {
  // Initial reading of battery statuses.
  WriteFile(peripheral_capacity_file_, base::NumberToString(80));
  battery_.Init(&test_wrapper_, &udev_);
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));

  EXPECT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(80, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());

  // An udev event appear for a non-peripheral device. Check that it is ignored.
  WriteFile(non_peripheral_capacity_file_, base::NumberToString(50));
  udev_.NotifySubsystemObservers({{PeripheralBatteryWatcher::kUdevSubsystem, "",
                                   kNonPeripheralBatterySysname, ""},
                                  UdevEvent::Action::CHANGE});
  EXPECT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));
}

TEST_F(PeripheralBatteryWatcherTest, RefreshBluetoothBattery) {
  battery_.Init(&test_wrapper_, &udev_);

  // Initialize non-Bluetooth peripheral.
  WriteFile(peripheral_capacity_file_, base::NumberToString(90));
  // Initialize Bluetooth peripheral.
  WriteFile(bluetooth_capacity_file_, base::NumberToString(80));

  // RefreshBluetoothBattery is called.
  dbus::MethodCall method_call(kPowerManagerInterface,
                               kRefreshBluetoothBatteryMethod);
  dbus::MessageWriter(&method_call).AppendString("11:22:33:AA:BB:CC");
  std::unique_ptr<dbus::Response> response =
      test_wrapper_.CallExportedMethodSync(&method_call);
  ASSERT_TRUE(response);
  ASSERT_EQ(dbus::Message::MESSAGE_METHOD_RETURN, response->GetMessageType());
  // Check that powerd reads the battery information and sends an update signal.
  ASSERT_TRUE(test_wrapper_.RunUntilSignalSent(kUpdateTimeout));
  ASSERT_EQ(1, test_wrapper_.num_sent_signals());
  PeripheralBatteryStatus proto;
  EXPECT_TRUE(test_wrapper_.GetSentSignal(0, kPeripheralBatteryStatusSignal,
                                          &proto, nullptr));
  EXPECT_EQ(80, proto.level());
  EXPECT_EQ(kDeviceModelName, proto.name());

  // RefreshBluetoothBattery is called for non-Bluetooth device.
  dbus::MethodCall method_call2(kPowerManagerInterface,
                                kRefreshBluetoothBatteryMethod);
  dbus::MessageWriter(&method_call2).AppendString("someperipheral");
  response = test_wrapper_.CallExportedMethodSync(&method_call2);
  ASSERT_TRUE(response);
  ASSERT_EQ(dbus::Message::MESSAGE_METHOD_RETURN, response->GetMessageType());
  // Check that powerd ignores the request.
  EXPECT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));

  // RefreshBluetoothBattery is called for non-existing device.
  dbus::MethodCall method_call3(kPowerManagerInterface,
                                kRefreshBluetoothBatteryMethod);
  dbus::MessageWriter(&method_call3).AppendString("non-existing");
  response = test_wrapper_.CallExportedMethodSync(&method_call3);
  ASSERT_TRUE(response);
  ASSERT_EQ(dbus::Message::MESSAGE_METHOD_RETURN, response->GetMessageType());
  // Check that powerd ignores the request.
  EXPECT_FALSE(test_wrapper_.RunUntilSignalSent(kShortUpdateTimeout));
}

}  // namespace system
}  // namespace power_manager
