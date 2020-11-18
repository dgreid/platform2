// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/optional.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

using ::testing::UnorderedElementsAreArray;

constexpr int kMaximumCycleCount = 5;
constexpr int kPercentBatteryWearAllowed = 10;
constexpr int kHighCycleCount = 6;
constexpr int kLowCycleCount = 4;
constexpr int kHighChargeFull = 91;
constexpr int kLowChargeFull = 89;
constexpr int kFakeBatteryChargeFullDesign = 100;
constexpr char kFakeManufacturer[] = "Fake Manufacturer";
constexpr double kFakeCurrentNow = 0.512;
constexpr int kFakePresent = 1;
constexpr char kFakeStatus[] = "Full";
constexpr double kFakeVoltageNow = 8.388;
constexpr double kFakeChargeNow = 6.154;

std::string ConstructOutput() {
  std::string output;
  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetIntKey("wearPercentage", 100 - (kHighChargeFull * 100 /
                                                 kFakeBatteryChargeFullDesign));
  result_dict.SetIntKey("cycleCount", kLowCycleCount);
  result_dict.SetStringKey("manufacturer", kFakeManufacturer);
  result_dict.SetIntKey("currentNowA", kFakeCurrentNow);
  result_dict.SetIntKey("present", kFakePresent);
  result_dict.SetStringKey("status", kFakeStatus);
  result_dict.SetIntKey("voltageNowV", kFakeVoltageNow);
  result_dict.SetIntKey("chargeFullAh", kHighChargeFull);
  result_dict.SetIntKey("chargeFullDesignAh", kFakeBatteryChargeFullDesign);
  result_dict.SetIntKey("chargeNowAh", kFakeChargeNow);
  base::Value output_dict(base::Value::Type::DICTIONARY);
  output_dict.SetKey("resultDetails", std::move(result_dict));
  base::JSONWriter::WriteWithOptions(
      output_dict, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &output);
  return output;
}

power_manager::PowerSupplyProperties GetDefaultPowerSupplyProperties() {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_vendor(kFakeManufacturer);
  power_supply_proto.set_battery_current(kFakeCurrentNow);
  power_supply_proto.set_battery_state(
      power_manager::PowerSupplyProperties_BatteryState_CHARGING);
  power_supply_proto.set_battery_status(kFakeStatus);
  power_supply_proto.set_battery_voltage(kFakeVoltageNow);
  power_supply_proto.set_battery_charge(kFakeChargeNow);
  return power_supply_proto;
}

}  // namespace

class BatteryHealthRoutineTest : public testing::Test {
 protected:
  BatteryHealthRoutineTest() = default;
  BatteryHealthRoutineTest(const BatteryHealthRoutineTest&) = delete;
  BatteryHealthRoutineTest& operator=(const BatteryHealthRoutineTest&) = delete;

  void SetUp() override { ASSERT_TRUE(mock_context_.Initialize()); }

  mojo_ipc::RoutineUpdate* update() { return &update_; }

  void CreateRoutine(
      uint32_t maximum_cycle_count = kMaximumCycleCount,
      uint32_t percent_battery_wear_allowed = kPercentBatteryWearAllowed) {
    routine_ = CreateBatteryHealthRoutine(&mock_context_, maximum_cycle_count,
                                          percent_battery_wear_allowed);
  }

  void RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    routine_->Start();

    // Since the BatteryHealthRoutine has finished by the time Start() returns,
    // there is no need to wait.
    routine_->PopulateStatusUpdate(&update_, true);
  }

  FakePowerdAdapter* fake_powerd_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

 private:
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that the battery health routine fails if the cycle count is too high.
TEST_F(BatteryHealthRoutineTest, HighCycleCount) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_charge_full(kHighChargeFull);
  power_supply_proto.set_battery_charge_full_design(
      kFakeBatteryChargeFullDesign);
  power_supply_proto.set_battery_cycle_count(kHighCycleCount);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatteryHealthExcessiveCycleCountMessage);
}

// Test that the battery health routine fails if cycle_count is not present.
TEST_F(BatteryHealthRoutineTest, NoCycleCount) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_charge_full(kHighChargeFull);
  power_supply_proto.set_battery_charge_full_design(
      kFakeBatteryChargeFullDesign);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryHealthFailedReadingCycleCountMessage);
}

// Test that the battery health routine fails if the wear percentage is too
// high.
TEST_F(BatteryHealthRoutineTest, HighWearPercentage) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_charge_full(kLowChargeFull);
  power_supply_proto.set_battery_charge_full_design(
      kFakeBatteryChargeFullDesign);
  power_supply_proto.set_battery_cycle_count(kLowCycleCount);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatteryHealthExcessiveWearMessage);
}

// Test that the battery health routine fails if neither charge_full nor
// energy_full are present.
TEST_F(BatteryHealthRoutineTest, NoWearPercentage) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_cycle_count(kLowCycleCount);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryHealthFailedCalculatingWearPercentageMessage);
}

// Test that the battery health routine passes if the cycle count and wear
// percentage are within acceptable limits.
TEST_F(BatteryHealthRoutineTest, GoodParameters) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_charge_full(kHighChargeFull);
  power_supply_proto.set_battery_charge_full_design(
      kFakeBatteryChargeFullDesign);
  power_supply_proto.set_battery_cycle_count(kLowCycleCount);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryHealthRoutinePassedMessage);

  auto shm_mapping = diagnostics::GetReadOnlySharedMemoryMappingFromMojoHandle(
      std::move(update()->output));
  ASSERT_TRUE(shm_mapping.IsValid());
  EXPECT_EQ(std::string(shm_mapping.GetMemoryAs<const char>(),
                        shm_mapping.mapped_size()),
            ConstructOutput());
}

// Test that the battery health routine catches invalid parameters.
TEST_F(BatteryHealthRoutineTest, InvalidParameters) {
  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  constexpr int kInvalidMaximumWearPercentage = 101;
  CreateRoutine(kMaximumCycleCount, kInvalidMaximumWearPercentage);
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryHealthInvalidParametersMessage);
}

// Test that the battery health routine handles a battery whose capacity exceeds
// its design capacity.
TEST_F(BatteryHealthRoutineTest, CapacityExceedsDesignCapacity) {
  // Set the capacity to anything higher than the design capacity.
  constexpr int kHigherCapacity = 100;
  constexpr int kLowerDesignCapacity = 20;

  auto power_supply_proto = GetDefaultPowerSupplyProperties();
  power_supply_proto.set_battery_charge_full(kHigherCapacity);
  power_supply_proto.set_battery_charge_full_design(kLowerDesignCapacity);
  power_supply_proto.set_battery_cycle_count(kLowCycleCount);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  // When the capacity exceeds the design capacity, the battery shouldn't be
  // worn at all.
  constexpr int kNotWornPercentage = 0;
  CreateRoutine(kMaximumCycleCount, kNotWornPercentage);
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryHealthRoutinePassedMessage);
}

// Test that the battery health routine fails when powerd returns an error.
TEST_F(BatteryHealthRoutineTest, PowerdError) {
  fake_powerd_adapter()->SetPowerSupplyProperties(base::nullopt);

  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kPowerdPowerSupplyPropertiesFailedMessage);
}

}  // namespace diagnostics
