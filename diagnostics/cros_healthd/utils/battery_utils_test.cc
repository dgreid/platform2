// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace diagnostics {

using ::dbus::MockBus;
using ::dbus::MockObjectProxy;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::WithArg;

namespace {

// The path used to check a device's master configuration hardware properties.
constexpr char kHardwarePropertiesPath[] = "/hardware-properties";
// The master configuration property that specifies a device's PSU type.
constexpr char kPsuTypeProperty[] = "psu-type";

// The path used to check a device's master configuration cros_healthd battery
// properties.
constexpr char kBatteryPropertiesPath[] = "/cros-healthd/battery";
// The master configuration property that indicates whether a device has Smart
// Battery info.
constexpr char kHasSmartBatteryInfoProperty[] = "has-smart-battery-info";

// Arbitrary test values for the various battery metrics.
constexpr power_manager::PowerSupplyProperties_BatteryState kBatteryStateFull =
    power_manager::PowerSupplyProperties_BatteryState_FULL;
constexpr char kBatteryVendor[] = "TEST_MFR";
constexpr double kBatteryVoltage = 127.45;
constexpr int kBatteryCycleCount = 2;
constexpr char kBatterySerialNumber[] = "1000";
constexpr double kBatteryVoltageMinDesign = 114.00;
constexpr double kBatteryChargeFull = 4.3;
constexpr double kBatteryChargeFullDesign = 3.92;
constexpr char kBatteryModelName[] = "TEST_MODEL_NAME";
constexpr double kBatteryChargeNow = 5.17;
constexpr char kSmartBatteryManufactureDate[] = "87615";
constexpr char kConvertedSmartBatteryManufactureDate[] = "2151-01-31";
constexpr char kBatteryTemperatureSmartChars[] = "981329";
constexpr int kBatteryTemperatureSmart = 981329;
constexpr double kBatteryCurrentNow = 6.45;
constexpr char kBatteryTechnology[] = "Battery technology.";
constexpr char kBatteryStatus[] = "Discharging";

// Timeouts for the D-Bus calls. Note that D-Bus is mocked out in the test, but
// the timeouts are still part of the mock calls.
constexpr int kDebugdTimeOut = 10 * 1000;
constexpr base::TimeDelta kPowerManagerDBusTimeout =
    base::TimeDelta::FromSeconds(3);

}  // namespace

class BatteryUtilsTest : public ::testing::Test {
 protected:
  BatteryUtilsTest() {
    options_.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options_);
    mock_power_manager_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), power_manager::kPowerManagerServiceName,
        dbus::ObjectPath(power_manager::kPowerManagerServicePath));
    fake_cros_config_ = std::make_unique<brillo::FakeCrosConfig>();
    battery_fetcher_ = std::make_unique<BatteryFetcher>(
        &mock_debugd_proxy_, mock_power_manager_proxy_.get(),
        fake_cros_config_.get());
  }

  void SetUp() override { SetHasSmartBatteryInfo("true"); }

  BatteryFetcher* battery_fetcher() { return battery_fetcher_.get(); }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return &mock_debugd_proxy_;
  }

  dbus::MockObjectProxy* mock_power_manager_proxy() {
    return mock_power_manager_proxy_.get();
  }

  void SetPsuType(const std::string& type) {
    fake_cros_config_->SetString(kHardwarePropertiesPath, kPsuTypeProperty,
                                 type);
  }

  void SetHasSmartBatteryInfo(const std::string& has_smart_battery_info) {
    fake_cros_config_->SetString(kBatteryPropertiesPath,
                                 kHasSmartBatteryInfoProperty,
                                 has_smart_battery_info);
  }

 private:
  dbus::Bus::Options options_;
  scoped_refptr<dbus::MockBus> mock_bus_;
  StrictMock<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  scoped_refptr<dbus::MockObjectProxy> mock_power_manager_proxy_;
  std::unique_ptr<brillo::FakeCrosConfig> fake_cros_config_;
  std::unique_ptr<BatteryFetcher> battery_fetcher_;
};

// Test that we can fetch all battery metrics correctly.
TEST_F(BatteryUtilsTest, FetchBatteryInfo) {
  // Create PowerSupplyProperties response protobuf.
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  power_supply_proto.set_battery_vendor(kBatteryVendor);
  power_supply_proto.set_battery_voltage(kBatteryVoltage);
  power_supply_proto.set_battery_cycle_count(kBatteryCycleCount);
  power_supply_proto.set_battery_charge_full(kBatteryChargeFull);
  power_supply_proto.set_battery_charge_full_design(kBatteryChargeFullDesign);
  power_supply_proto.set_battery_serial_number(kBatterySerialNumber);
  power_supply_proto.set_battery_voltage_min_design(kBatteryVoltageMinDesign);
  power_supply_proto.set_battery_model_name(kBatteryModelName);
  power_supply_proto.set_battery_charge(kBatteryChargeNow);
  power_supply_proto.set_battery_current(kBatteryCurrentNow);
  power_supply_proto.set_battery_technology(kBatteryTechnology);
  power_supply_proto.set_battery_status(kBatteryStatus);

  // Set the mock power manager response.
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce([&power_supply_proto](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> power_manager_response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter power_manager_writer(power_manager_response.get());
        power_manager_writer.AppendProtoAsArrayOfBytes(power_supply_proto);
        return power_manager_response;
      });

  // Set the mock Debugd Adapter responses.
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("manufacture_date_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<1>(Invoke([](std::string* result) {
                        *result = kSmartBatteryManufactureDate;
                      })),
                      Return(true)));
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("temperature_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<1>(Invoke([](std::string* result) {
                        *result = kBatteryTemperatureSmartChars;
                      })),
                      Return(true)));

  auto battery = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery);

  EXPECT_EQ(kBatteryCycleCount, battery->cycle_count);
  EXPECT_EQ(kBatteryVendor, battery->vendor);
  EXPECT_EQ(kBatteryVoltage, battery->voltage_now);
  EXPECT_EQ(kBatteryChargeFull, battery->charge_full);
  EXPECT_EQ(kBatteryChargeFullDesign, battery->charge_full_design);
  EXPECT_EQ(kBatterySerialNumber, battery->serial_number);
  EXPECT_EQ(kBatteryVoltageMinDesign, battery->voltage_min_design);
  EXPECT_EQ(kBatteryModelName, battery->model_name);
  EXPECT_EQ(kBatteryChargeNow, battery->charge_now);
  EXPECT_EQ(kBatteryCurrentNow, battery->current_now);
  EXPECT_EQ(kBatteryTechnology, battery->technology);
  EXPECT_EQ(kBatteryStatus, battery->status);
  EXPECT_EQ(kConvertedSmartBatteryManufactureDate,
            battery->smart_battery_info->manufacture_date);
  EXPECT_EQ(kBatteryTemperatureSmart, battery->smart_battery_info->temperature);
}

// Test that we handle a malformed power_manager D-Bus response.
TEST_F(BatteryUtilsTest, MalformedPowerManagerDbusResponse) {
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  auto battery = battery_fetcher()->FetchBatteryInfo();
  EXPECT_FALSE(battery);
}

// Test that we handle an empty proto in a power_manager D-Bus response.
TEST_F(BatteryUtilsTest, EmptyProtoPowerManagerDbusResponse) {
  power_manager::PowerSupplyProperties power_supply_proto;

  // Set the mock power manager response.
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce([&power_supply_proto](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> power_manager_response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter power_manager_writer(power_manager_response.get());
        power_manager_writer.AppendProtoAsArrayOfBytes(power_supply_proto);
        return power_manager_response;
      });

  auto battery = battery_fetcher()->FetchBatteryInfo();
  EXPECT_FALSE(battery);
}

// Test that we handle debugd failing to collect smart metrics.
TEST_F(BatteryUtilsTest, SmartMetricRetrievalFailure) {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);

  // Set the mock power manager response.
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce([&power_supply_proto](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> power_manager_response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter power_manager_writer(power_manager_response.get());
        power_manager_writer.AppendProtoAsArrayOfBytes(power_supply_proto);
        return power_manager_response;
      });

  // Set the mock Debugd Adapter responses.
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("manufacture_date_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("temperature_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));

  auto battery = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery);

  EXPECT_EQ("0000-00-00", battery->smart_battery_info->manufacture_date);
  EXPECT_EQ(0, battery->smart_battery_info->temperature);
}

// Test that Smart Battery metrics are not fetched when a device does not have a
// Smart Battery.
TEST_F(BatteryUtilsTest, NoSmartBattery) {
  SetHasSmartBatteryInfo("false");

  // Set the mock power manager response.
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_state(kBatteryStateFull);
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce([&power_supply_proto](dbus::MethodCall*, int) {
        std::unique_ptr<dbus::Response> power_manager_response =
            dbus::Response::CreateEmpty();
        dbus::MessageWriter power_manager_writer(power_manager_response.get());
        power_manager_writer.AppendProtoAsArrayOfBytes(power_supply_proto);
        return power_manager_response;
      });

  auto battery = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery);

  EXPECT_TRUE(battery->smart_battery_info.is_null());
}

// Test that no battery info is returned when a device does not have a battery.
TEST_F(BatteryUtilsTest, NoBattery) {
  SetPsuType("AC_only");
  auto battery = battery_fetcher()->FetchBatteryInfo();
  EXPECT_FALSE(battery);
}

}  // namespace diagnostics
