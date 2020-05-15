// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <map>
#include <memory>
#include <utility>

#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/power_manager/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "debugd/dbus-proxy-mocks.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

namespace diagnostics {

using ::chromeos::cros_healthd::mojom::ErrorType;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
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
constexpr char kSmartBatteryManufactureDateResponse[] =
    "Read from I2C port 2 at 0xb offset 0x1b = 0x4d06";
constexpr char kSmartBatteryManufactureDate[] = "2018-08-06";
constexpr char kSmartBatteryTemperatureResponse[] =
    "Read from I2C port 2 at 0xb offset 0x8 = 0xbae";
constexpr uint64_t kSmartBatteryTemperature = 2990;
constexpr char kInvalidRegexSmartMetricResponse[] =
    "this does not match the regex";
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
  BatteryUtilsTest() = default;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    SetHasSmartBatteryInfo("true");
  }

  BatteryFetcher* battery_fetcher() { return &battery_fetcher_; }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return mock_context_.mock_debugd_proxy();
  }

  dbus::MockObjectProxy* mock_power_manager_proxy() {
    return mock_context_.mock_power_manager_proxy();
  }

  void SetPsuType(const std::string& type) {
    mock_context_.fake_cros_config()->SetString(kHardwarePropertiesPath,
                                                kPsuTypeProperty, type);
  }

  void SetHasSmartBatteryInfo(const std::string& has_smart_battery_info) {
    mock_context_.fake_cros_config()->SetString(kBatteryPropertiesPath,
                                                kHasSmartBatteryInfoProperty,
                                                has_smart_battery_info);
  }

 private:
  MockContext mock_context_;
  BatteryFetcher battery_fetcher_{&mock_context_};
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
                        *result = kSmartBatteryManufactureDateResponse;
                      })),
                      Return(true)));
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("temperature_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<1>(Invoke([](std::string* result) {
                        *result = kSmartBatteryTemperatureResponse;
                      })),
                      Return(true)));

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_battery_info());

  const auto& battery = battery_result->get_battery_info();
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

  // Test that optional smart battery metrics are populated.
  ASSERT_TRUE(battery->manufacture_date.has_value());
  ASSERT_TRUE(battery->temperature);
  EXPECT_EQ(kSmartBatteryManufactureDate, battery->manufacture_date.value());
  EXPECT_EQ(kSmartBatteryTemperature, battery->temperature->value);
}

// Test that a malformed power_manager D-Bus response returns an error.
TEST_F(BatteryUtilsTest, MalformedPowerManagerDbusResponse) {
  EXPECT_CALL(*mock_power_manager_proxy(),
              CallMethodAndBlock(_, kPowerManagerDBusTimeout.InMilliseconds()))
      .WillOnce(
          [](dbus::MethodCall*, int) { return dbus::Response::CreateEmpty(); });

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type, ErrorType::kParseError);
}

// Test that an empty proto in a power_manager D-Bus response returns an error.
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

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type, ErrorType::kSystemUtilityError);
}

// Test that debugd failing to collect battery manufacture date returns an
// error.
TEST_F(BatteryUtilsTest, ManufactureDateRetrievalFailure) {
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

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type, ErrorType::kSystemUtilityError);
}

// Test that debugd failing to collect battery temperature returns an error.
TEST_F(BatteryUtilsTest, TemperatureRetrievalFailure) {
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
      .WillOnce(DoAll(WithArg<1>(Invoke([](std::string* result) {
                        *result = kSmartBatteryManufactureDateResponse;
                      })),
                      Return(true)));
  EXPECT_CALL(
      *mock_debugd_proxy(),
      CollectSmartBatteryMetric("temperature_smart", _, _, kDebugdTimeOut))
      .WillOnce(DoAll(WithArg<2>(Invoke([](brillo::ErrorPtr* error) {
                        *error = brillo::Error::Create(FROM_HERE, "", "", "");
                      })),
                      Return(false)));

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type, ErrorType::kSystemUtilityError);
}

// Test that failing to match the regex to the debugd responses returns an
// error.
TEST_F(BatteryUtilsTest, SmartMetricRegexFailure) {
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
      .WillOnce(DoAll(WithArg<1>(Invoke([](std::string* result) {
                        *result = kInvalidRegexSmartMetricResponse;
                      })),
                      Return(true)));

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_error());
  EXPECT_EQ(battery_result->get_error()->type, ErrorType::kParseError);
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

  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->is_battery_info());
  const auto& battery = battery_result->get_battery_info();

  EXPECT_FALSE(battery->manufacture_date.has_value());
  EXPECT_FALSE(battery->temperature);
}

// Test that no battery info is returned when a device does not have a battery.
TEST_F(BatteryUtilsTest, NoBattery) {
  SetPsuType("AC_only");
  auto battery_result = battery_fetcher()->FetchBatteryInfo();
  ASSERT_TRUE(battery_result->get_battery_info().is_null());
}

}  // namespace diagnostics
