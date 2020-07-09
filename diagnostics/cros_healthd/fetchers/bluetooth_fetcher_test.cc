// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <dbus/object_path.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/common/system/fake_bluetooth_client.h"
#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"

namespace diagnostics {

namespace {

using ::testing::Return;
using ::testing::StrictMock;

std::unique_ptr<BluetoothClient::AdapterProperties> GetAdapterProperties() {
  auto properties = std::make_unique<BluetoothClient::AdapterProperties>(
      nullptr, base::Bind([](const std::string& property_name) {}));
  properties->address.ReplaceValue("aa:bb:cc:dd:ee:ff");
  properties->name.ReplaceValue("sarien-laptop");
  properties->powered.ReplaceValue(true);
  properties->address.set_valid(true);
  properties->name.set_valid(true);
  properties->powered.set_valid(true);
  return properties;
}

std::unique_ptr<BluetoothClient::DeviceProperties> GetDeviceProperties() {
  auto properties = std::make_unique<BluetoothClient::DeviceProperties>(
      nullptr, base::Bind([](const std::string& property_name) {}));
  properties->address.ReplaceValue("70:88:6B:92:34:70");
  properties->name.ReplaceValue("GID6B");
  properties->connected.ReplaceValue(true);
  properties->adapter.ReplaceValue(dbus::ObjectPath("/org/bluez/hci0"));
  properties->address.set_valid(true);
  properties->name.set_valid(true);
  properties->connected.set_valid(true);
  properties->adapter.set_valid(true);
  return properties;
}

}  // namespace

class BluetoothUtilsTest : public ::testing::Test {
 protected:
  BluetoothUtilsTest() = default;
  BluetoothUtilsTest(const BluetoothUtilsTest&) = delete;
  BluetoothUtilsTest& operator=(const BluetoothUtilsTest&) = delete;
  ~BluetoothUtilsTest() = default;

  void SetUp() override { ASSERT_TRUE(mock_context_.Initialize()); }

  BluetoothFetcher* bluetooth_fetcher() { return &bluetooth_fetcher_; }

  FakeBluetoothClient* fake_bluetooth_client() {
    return mock_context_.fake_bluetooth_client();
  }

  dbus::ObjectPath adapter_path() {
    return dbus::ObjectPath("/org/bluez/hci0");
  }

  dbus::ObjectPath device_path() {
    return dbus::ObjectPath("/org/bluez/hci0/dev_70_88_6B_92_34_70");
  }

 private:
  MockContext mock_context_;
  BluetoothFetcher bluetooth_fetcher_{&mock_context_};
};

// Test that Bluetooth info can be fetched successfully.
TEST_F(BluetoothUtilsTest, FetchBluetoothInfo) {
  const std::unique_ptr<BluetoothClient::AdapterProperties> kAdapterProperties =
      GetAdapterProperties();
  const std::unique_ptr<BluetoothClient::DeviceProperties> kDeviceProperties =
      GetDeviceProperties();

  EXPECT_CALL(*fake_bluetooth_client(), GetAdapters())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{adapter_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapterProperties(adapter_path()))
      .WillOnce(Return(kAdapterProperties.get()));
  EXPECT_CALL(*fake_bluetooth_client(), GetDevices())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{device_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetDeviceProperties(device_path()))
      .WillOnce(Return(kDeviceProperties.get()));

  auto bluetooth_result = bluetooth_fetcher()->FetchBluetoothInfo();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->name, kAdapterProperties->name.value());
  EXPECT_EQ(adapter_info[0]->address, kAdapterProperties->address.value());
  EXPECT_TRUE(adapter_info[0]->powered);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 1);
}

// Test that getting no adapter and device objects is handled gracefully.
TEST_F(BluetoothUtilsTest, NoObjects) {
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapters())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{}));
  EXPECT_CALL(*fake_bluetooth_client(), GetDevices())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{}));

  auto bluetooth_result = bluetooth_fetcher()->FetchBluetoothInfo();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 0);
}

// Test that getting no adapter and device properties is handled gracefully.
TEST_F(BluetoothUtilsTest, NoProperties) {
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapters())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{adapter_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapterProperties(adapter_path()))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*fake_bluetooth_client(), GetDevices())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{device_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetDeviceProperties(device_path()))
      .WillOnce(Return(nullptr));

  auto bluetooth_result = bluetooth_fetcher()->FetchBluetoothInfo();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  EXPECT_EQ(adapter_info.size(), 0);
}

// Test that the number of connected devices is counted correctly.
TEST_F(BluetoothUtilsTest, NumConnectedDevices) {
  const std::unique_ptr<BluetoothClient::AdapterProperties> kAdapterProperties =
      GetAdapterProperties();
  const std::unique_ptr<BluetoothClient::DeviceProperties> kDeviceProperties =
      GetDeviceProperties();

  EXPECT_CALL(*fake_bluetooth_client(), GetAdapters())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{adapter_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapterProperties(adapter_path()))
      .WillOnce(Return(kAdapterProperties.get()));
  EXPECT_CALL(*fake_bluetooth_client(), GetDevices())
      .WillOnce(
          Return(std::vector<dbus::ObjectPath>{device_path(), device_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetDeviceProperties(device_path()))
      .Times(2)
      .WillRepeatedly(Return(kDeviceProperties.get()));

  auto bluetooth_result = bluetooth_fetcher()->FetchBluetoothInfo();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 2);
}

// Test that a disconnected device is not counted as a connected device.
TEST_F(BluetoothUtilsTest, DisconnectedDevice) {
  const std::unique_ptr<BluetoothClient::AdapterProperties> kAdapterProperties =
      GetAdapterProperties();
  std::unique_ptr<BluetoothClient::DeviceProperties> device_properties =
      GetDeviceProperties();
  device_properties->connected.ReplaceValue(false);

  EXPECT_CALL(*fake_bluetooth_client(), GetAdapters())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{adapter_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetAdapterProperties(adapter_path()))
      .WillOnce(Return(kAdapterProperties.get()));
  EXPECT_CALL(*fake_bluetooth_client(), GetDevices())
      .WillOnce(Return(std::vector<dbus::ObjectPath>{device_path()}));
  EXPECT_CALL(*fake_bluetooth_client(), GetDeviceProperties(device_path()))
      .WillOnce(Return(device_properties.get()));

  auto bluetooth_result = bluetooth_fetcher()->FetchBluetoothInfo();
  ASSERT_TRUE(bluetooth_result->is_bluetooth_adapter_info());
  const auto& adapter_info = bluetooth_result->get_bluetooth_adapter_info();
  ASSERT_EQ(adapter_info.size(), 1);
  EXPECT_EQ(adapter_info[0]->num_connected_devices, 0);
}

}  // namespace diagnostics
