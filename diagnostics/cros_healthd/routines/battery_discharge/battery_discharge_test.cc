// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/common/system/fake_powerd_adapter.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr double kStartingChargePercent = 80;
constexpr double kEndingChargePercent = 55;

// With this value for maximum_discharge_percent_allowed, the routine should
// pass.
constexpr uint32_t kPassingPercent = 50;
// With this value for maximum_discharge_percent_allowed, the routine should
// fail.
constexpr uint32_t kFailingPercent = 1;
// With this value for maximum_discharge_percent_allowed, the routine should
// error out.
constexpr uint32_t kErrorPercent = 101;

constexpr base::TimeDelta kFullDuration = base::TimeDelta::FromSeconds(12);
constexpr base::TimeDelta kHalfDuration = kFullDuration / 2;
constexpr base::TimeDelta kQuarterDuration = kFullDuration / 4;

power_manager::PowerSupplyProperties GetPowerSupplyProperties() {
  power_manager::PowerSupplyProperties power_supply_proto;
  power_supply_proto.set_battery_percent(kStartingChargePercent);
  power_supply_proto.set_battery_state(
      power_manager::PowerSupplyProperties_BatteryState_DISCHARGING);
  return power_supply_proto;
}

}  // namespace

class BatteryDischargeRoutineTest : public testing::Test {
 protected:
  BatteryDischargeRoutineTest() = default;
  BatteryDischargeRoutineTest(const BatteryDischargeRoutineTest&) = delete;
  BatteryDischargeRoutineTest& operator=(const BatteryDischargeRoutineTest&) =
      delete;

  void SetUp() override { ASSERT_TRUE(mock_context_.Initialize()); }

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateRoutine(uint32_t maximum_discharge_percent_allowed) {
    routine_ = std::make_unique<BatteryDischargeRoutine>(
        mock_context(), kFullDuration, maximum_discharge_percent_allowed,
        task_environment_.GetMockTickClock());
  }

  void StartRoutineAndVerifyInteractiveResponse() {
    DCHECK(routine_);

    routine_->Start();
    auto update = GetUpdate();
    VerifyInteractiveUpdate(
        update->routine_update_union,
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower);
    EXPECT_EQ(update->progress_percent, 0);
  }

  mojo_ipc::RoutineUpdatePtr GetUpdate() {
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  MockContext* mock_context() { return &mock_context_; }

  FakePowerdAdapter* fake_powerd_adapter() {
    return mock_context_.fake_powerd_adapter();
  }

  MockContext mock_context_;
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<BatteryDischargeRoutine> routine_;
};

// Test that the routine can be created with the default tick clock and root
// directory.
TEST_F(BatteryDischargeRoutineTest, DefaultConstruction) {
  BatteryDischargeRoutine routine{mock_context(), kFullDuration,
                                  kPassingPercent};
  EXPECT_EQ(routine.GetStatus(), mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

// Test that the routine passes when the battery discharges less than
// maximum_discharge_percent_allowed.
TEST_F(BatteryDischargeRoutineTest, RoutineSuccess) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryDischargeRoutineSucceededMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine fails when the battery discharges more than
// maximum_discharge_percent_allowed.
TEST_F(BatteryDischargeRoutineTest, ExceedMaxDischargeFailure) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kFailingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
      kBatteryDischargeRoutineFailedExcessiveDischargeMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine handles an invalid maximum_discharge_percent_allowed
// input.
TEST_F(BatteryDischargeRoutineTest, InvalidParameters) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kErrorPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryDischargeRoutineInvalidParametersMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

// Test that the routine handles the battery not discharging.
TEST_F(BatteryDischargeRoutineTest, BatteryNotDischarging) {
  auto power_supply_proto = GetPowerSupplyProperties();
  power_supply_proto.set_battery_state(
      power_manager::PowerSupplyProperties_BatteryState_CHARGING);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryDischargeRoutineNotDischargingMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

// Test that the routine handles an ending charge higher than the starting
// charge.
TEST_F(BatteryDischargeRoutineTest, EndingChargeHigherThanStartingCharge) {
  auto power_supply_proto = GetPowerSupplyProperties();
  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  power_supply_proto.set_battery_percent(kStartingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryDischargeRoutineNotDischargingMessage);
  EXPECT_EQ(update->progress_percent, 50);
}

// Test that the routine handles an error from powerd.
TEST_F(BatteryDischargeRoutineTest, PowerdError) {
  fake_powerd_adapter()->SetPowerSupplyProperties(base::nullopt);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kPowerdPowerSupplyPropertiesFailedMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

// Test that the routine handles an error from powerd after the delayed task.
TEST_F(BatteryDischargeRoutineTest, DelayedTaskPowerdError) {
  auto power_supply_proto = GetPowerSupplyProperties();
  power_supply_proto.set_battery_percent(kEndingChargePercent);
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  fake_powerd_adapter()->SetPowerSupplyProperties(base::nullopt);

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kPowerdPowerSupplyPropertiesFailedMessage);
  EXPECT_EQ(update->progress_percent, 50);
}

// Test that we can cancel the routine in its waiting state.
TEST_F(BatteryDischargeRoutineTest, CancelWhileWaiting) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  routine()->Start();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);

  routine()->Cancel();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
                             kBatteryDischargeRoutineCancelledMessage);
  EXPECT_EQ(update->progress_percent, 0);

  FastForwardBy(kFullDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
                             kBatteryDischargeRoutineCancelledMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

// Test that we can cancel the routine partway through running.
TEST_F(BatteryDischargeRoutineTest, CancelWhileRunning) {
  auto power_supply_proto = GetPowerSupplyProperties();
  fake_powerd_adapter()->SetPowerSupplyProperties(power_supply_proto);

  CreateRoutine(kPassingPercent);
  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  FastForwardBy(kQuarterDuration);
  routine()->Cancel();

  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
                             kBatteryDischargeRoutineCancelledMessage);
  EXPECT_EQ(update->progress_percent, 75);

  FastForwardBy(kQuarterDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
                             kBatteryDischargeRoutineCancelledMessage);
  EXPECT_EQ(update->progress_percent, 75);
}

// Test that cancelling a routine in an error state doesn't overwrite the state.
TEST_F(BatteryDischargeRoutineTest, CancelWhileInErrorState) {
  fake_powerd_adapter()->SetPowerSupplyProperties(base::nullopt);
  CreateRoutine(kPassingPercent);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kPowerdPowerSupplyPropertiesFailedMessage);
  EXPECT_EQ(update->progress_percent, 0);

  FastForwardBy(kQuarterDuration);
  routine()->Cancel();

  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kPowerdPowerSupplyPropertiesFailedMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

}  // namespace diagnostics
