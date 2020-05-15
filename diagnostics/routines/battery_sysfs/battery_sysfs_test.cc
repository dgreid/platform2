// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_split.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/routines/battery_sysfs/battery_sysfs.h"
#include "diagnostics/routines/routine_test_utils.h"
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
constexpr int kFakeCurrentNow = 90871023;
constexpr int kFakePresent = 1;
constexpr char kFakeStatus[] = "Full";
constexpr int kFakeVoltageNow = 90872;
constexpr int kFakeChargeNow = 98123;
constexpr char kFullCycleCountPath[] =
    "sys/class/power_supply/BAT0/cycle_count";

std::string ConstructOutput() {
  std::string output;
  int wear_percentage =
      100 - (kHighChargeFull * 100 / kFakeBatteryChargeFullDesign);
  output += "Wear Percentage: " + std::to_string(wear_percentage) + "\n";
  output += "Cycle Count: " + std::to_string(kLowCycleCount) + "\n";
  output += "Manufacturer: " + std::string(kFakeManufacturer) + "\n";
  output += "Current Now: " + std::to_string(kFakeCurrentNow) + "\n";
  output += "Present: " + std::to_string(kFakePresent) + "\n";
  output += "Status: " + std::string(kFakeStatus) + "\n";
  output += "Voltage Now: " + std::to_string(kFakeVoltageNow) + "\n";
  output += "Charge Full: " + std::to_string(kHighChargeFull) + "\n";
  output +=
      "Charge Full Design: " + std::to_string(kFakeBatteryChargeFullDesign) +
      "\n";
  output += "Charge Now: " + std::to_string(kFakeChargeNow) + "\n";
  return output;
}

}  // namespace

class BatterySysfsRoutineTest : public testing::Test {
 protected:
  BatterySysfsRoutineTest() { mojo::core::Init(); }
  BatterySysfsRoutineTest(const BatterySysfsRoutineTest&) = delete;
  BatterySysfsRoutineTest& operator=(const BatterySysfsRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojo_ipc::RoutineUpdate* update() { return &update_; }

  void CreateRoutine(
      uint32_t maximum_cycle_count = kMaximumCycleCount,
      uint32_t percent_battery_wear_allowed = kPercentBatteryWearAllowed) {
    routine_ = std::make_unique<BatterySysfsRoutine>(
        maximum_cycle_count, percent_battery_wear_allowed);
    routine_->set_root_dir_for_testing(temp_dir_.GetPath());
  }

  void RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    routine_->Start();

    // Since the BatterySysfsRoutine has finished by the time Start() returns,
    // there is no need to wait.
    routine_->PopulateStatusUpdate(&update_, true);
  }

  void WriteFilesReadByLog() {
    WriteFileContents(kBatterySysfsManufacturerPath, kFakeManufacturer);
    WriteFileContents(kBatterySysfsCurrentNowPath,
                      std::to_string(kFakeCurrentNow));
    WriteFileContents(kBatterySysfsPresentPath, std::to_string(kFakePresent));
    WriteFileContents(kBatterySysfsStatusPath, kFakeStatus);
    WriteFileContents(kBatterySysfsVoltageNowPath,
                      std::to_string(kFakeVoltageNow));
    WriteFileContents(kBatterySysfsChargeNowPath,
                      std::to_string(kFakeChargeNow));
  }

  void WriteFileContents(const std::string& relative_file_path,
                         const std::string& file_contents) {
    EXPECT_TRUE(
        WriteFileAndCreateParentDirs(temp_dir_path()
                                         .AppendASCII(kBatterySysfsPath)
                                         .AppendASCII(relative_file_path),
                                     file_contents));
  }

  const base::FilePath& temp_dir_path() const { return temp_dir_.GetPath(); }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<BatterySysfsRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that the battery_sysfs routine fails if the cycle count is too high.
TEST_F(BatterySysfsRoutineTest, HighCycleCount) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatterySysfsCycleCountPath,
                    std::to_string(kHighCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatterySysfsExcessiveCycleCountMessage);
}

// Test that the battery_sysfs routine fails if cycle_count is not present.
TEST_F(BatterySysfsRoutineTest, NoCycleCount) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatterySysfsFailedReadingCycleCountMessage);
}

// Test that the battery_sysfs routine fails if the wear percentage is too
// high.
TEST_F(BatterySysfsRoutineTest, HighWearPercentage) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kLowChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatterySysfsCycleCountPath,
                    std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatterySysfsExcessiveWearMessage);
}

// Test that the battery_sysfs routine fails if neither charge_full nor
// energy_full are present.
TEST_F(BatterySysfsRoutineTest, NoWearPercentage) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsCycleCountPath,
                    std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatterySysfsFailedCalculatingWearPercentageMessage);
}

// Test that the battery_sysfs routine passes if the cycle count and wear
// percentage are within acceptable limits.
TEST_F(BatterySysfsRoutineTest, GoodParameters) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatterySysfsCycleCountPath,
                    std::to_string(kLowCycleCount));
  WriteFilesReadByLog();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatterySysfsRoutinePassedMessage);

  base::StringPairs expected_output_pairs;
  base::StringPairs actual_output_pairs;
  ASSERT_TRUE(base::SplitStringIntoKeyValuePairs(ConstructOutput(), ':', '\n',
                                                 &expected_output_pairs));
  auto shared_memory = diagnostics::GetReadOnlySharedMemoryFromMojoHandle(
      std::move(update()->output));
  ASSERT_TRUE(shared_memory);
  ASSERT_TRUE(base::SplitStringIntoKeyValuePairs(
      base::StringPiece(static_cast<const char*>(shared_memory->memory()),
                        shared_memory->mapped_size()),
      ':', '\n', &actual_output_pairs));
  EXPECT_THAT(actual_output_pairs,
              UnorderedElementsAreArray(expected_output_pairs));
}

// Test that the battery_sysfs routine will find energy-reporting batteries.
TEST_F(BatterySysfsRoutineTest, EnergyReportingBattery) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsEnergyFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsEnergyFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatterySysfsCycleCountPath,
                    std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatterySysfsRoutinePassedMessage);
}

// Test that the battery_sysfs routine uses the expected full path to
// cycle_count, relative to the temporary test directory.
TEST_F(BatterySysfsRoutineTest, FullCycleCountPath) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path().AppendASCII(kFullCycleCountPath),
      std::to_string(kLowCycleCount)));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatterySysfsRoutinePassedMessage);
}

// Test that the battery_sysfs routine catches invalid parameters.
TEST_F(BatterySysfsRoutineTest, InvalidParameters) {
  constexpr int kInvalidMaximumWearPercentage = 101;
  CreateRoutine(kMaximumCycleCount, kInvalidMaximumWearPercentage);
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatterySysfsInvalidParametersMessage);
}

// Test that the battery_sysfs routine handles a battery whose capacity exceeds
// its design capacity.
TEST_F(BatterySysfsRoutineTest, CapacityExceedsDesignCapacity) {
  // When the capacity exceeds the design capacity, the battery shouldn't be
  // worn at all.
  constexpr int kNotWornPercentage = 0;
  CreateRoutine(kMaximumCycleCount, kNotWornPercentage);
  // Set the capacity to anything higher than the design capacity.
  constexpr int kHigherCapacity = 100;
  constexpr int kLowerDesignCapacity = 20;
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHigherCapacity));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kLowerDesignCapacity));
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path().AppendASCII(kFullCycleCountPath),
      std::to_string(kLowCycleCount)));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatterySysfsRoutinePassedMessage);
}

// Test that the battery_sysfs routine fails when invalid file contents are
// read.
TEST_F(BatterySysfsRoutineTest, InvalidFileContents) {
  CreateRoutine();
  WriteFileContents(kBatterySysfsChargeFullPath,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatterySysfsChargeFullDesignPath,
                    std::to_string(kFakeBatteryChargeFullDesign));
  constexpr char kInvalidUnsignedInt[] = "Invalid unsigned int!";
  EXPECT_TRUE(WriteFileAndCreateParentDirs(
      temp_dir_path().AppendASCII(kFullCycleCountPath), kInvalidUnsignedInt));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatterySysfsFailedReadingCycleCountMessage);
}

// Test that calling resume doesn't crash.
TEST_F(BatterySysfsRoutineTest, Resume) {
  CreateRoutine();
  routine()->Resume();
}

// Test that calling cancel doesn't crash.
TEST_F(BatterySysfsRoutineTest, Cancel) {
  CreateRoutine();
  routine()->Cancel();
}

// Test that we can retrieve the status of the battery_sysfs routine.
TEST_F(BatterySysfsRoutineTest, GetStatus) {
  CreateRoutine();
  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

}  // namespace diagnostics
