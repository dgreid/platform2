// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr uint32_t kStartingChargeNowFileContents = 4031000;
constexpr uint32_t kEndingChargeNowFileContents = 3000000;
constexpr uint32_t kChargeFullFileContents = 5042000;

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

}  // namespace

class BatteryDischargeRoutineTest : public testing::Test {
 protected:
  BatteryDischargeRoutineTest() { mojo::core::Init(); }
  BatteryDischargeRoutineTest(const BatteryDischargeRoutineTest&) = delete;
  BatteryDischargeRoutineTest& operator=(const BatteryDischargeRoutineTest&) =
      delete;

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateRoutine(uint32_t maximum_discharge_percent_allowed) {
    routine_ = std::make_unique<BatteryDischargeRoutine>(
        kFullDuration, maximum_discharge_percent_allowed, temp_dir_.GetPath(),
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

  void WriteBatterySysfsNode(const std::string& file_name,
                             const std::string& file_contents) {
    EXPECT_TRUE(
        WriteFileAndCreateParentDirs(temp_dir_.GetPath()
                                         .AppendASCII(kBatteryDirectoryPath)
                                         .AppendASCII(file_name),
                                     file_contents));
  }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  const base::FilePath GetTempPath() { return temp_dir_.GetPath(); }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<BatteryDischargeRoutine> routine_;
};

// Test that the routine can be created with the default tick clock and root
// directory.
TEST_F(BatteryDischargeRoutineTest, DefaultConstruction) {
  BatteryDischargeRoutine routine{kFullDuration, kPassingPercent};
  EXPECT_EQ(routine.GetStatus(), mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

// Test that the routine passes when the battery discharges less than
// maximum_discharge_percent_allowed.
TEST_F(BatteryDischargeRoutineTest, RoutineSuccess) {
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kEndingChargeNowFileContents));

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
  CreateRoutine(kFailingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kEndingChargeNowFileContents));

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
  CreateRoutine(kErrorPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

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
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusChargingValue);

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
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kEndingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryDischargeRoutineNotDischargingMessage);
  EXPECT_EQ(update->progress_percent, 50);
}

// Test that the routine handles a file going missing after the delayed task.
TEST_F(BatteryDischargeRoutineTest, DelayedTaskHasMissingFile) {
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kEndingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  FastForwardBy(kHalfDuration);
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
                             kBatteryDischargeRoutineRunningMessage);
  EXPECT_EQ(update->progress_percent, 50);

  ASSERT_TRUE(base::DeleteFile(GetTempPath()
                                   .AppendASCII(kBatteryDirectoryPath)
                                   .AppendASCII(kBatteryChargeNowFileName),
                               false /* recursive */));

  FastForwardBy(kHalfDuration);
  update = GetUpdate();
  VerifyNonInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage);
  EXPECT_EQ(update->progress_percent, 50);
}

// Test that we can cancel the routine in its waiting state.
TEST_F(BatteryDischargeRoutineTest, CancelWhileWaiting) {
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kStartingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

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
  CreateRoutine(kPassingPercent);
  WriteBatterySysfsNode(kBatteryChargeNowFileName,
                        std::to_string(kEndingChargeNowFileContents));
  WriteBatterySysfsNode(kBatteryChargeFullFileName,
                        std::to_string(kChargeFullFileContents));
  WriteBatterySysfsNode(kBatteryStatusFileName, kBatteryStatusDischargingValue);

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
  CreateRoutine(kPassingPercent);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage);
  EXPECT_EQ(update->progress_percent, 0);

  FastForwardBy(kQuarterDuration);
  routine()->Cancel();

  update = GetUpdate();
  VerifyNonInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

// Tests for the BatteryDischargeRoutine when various files are missing from
// sysfs.
//
// This is a parameterized test with the following parameter:
// * |file_name| - name of the missing file.
class MissingFileTest : public BatteryDischargeRoutineTest,
                        public testing::WithParamInterface<std::string> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():

  const std::string param() const { return GetParam(); }
};

// Test that the routine deals appropriately with missing files.
TEST_P(MissingFileTest, MisingFile) {
  CreateRoutine(kPassingPercent);
  if (param() != kBatteryChargeNowFileName)
    WriteBatterySysfsNode(kBatteryChargeNowFileName,
                          std::to_string(kStartingChargeNowFileContents));
  if (param() != kBatteryChargeFullFileName)
    WriteBatterySysfsNode(kBatteryChargeFullFileName,
                          std::to_string(kChargeFullFileContents));
  if (param() != kBatteryStatusFileName)
    WriteBatterySysfsNode(kBatteryStatusFileName,
                          kBatteryStatusDischargingValue);

  StartRoutineAndVerifyInteractiveResponse();

  routine()->Resume();
  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage);
  EXPECT_EQ(update->progress_percent, 0);
}

INSTANTIATE_TEST_SUITE_P(,
                         MissingFileTest,
                         testing::Values(kBatteryChargeNowFileName,
                                         kBatteryChargeFullFileName,
                                         kBatteryStatusFileName));

}  // namespace diagnostics
