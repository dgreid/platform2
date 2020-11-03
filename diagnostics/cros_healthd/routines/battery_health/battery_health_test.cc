// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_writer.h>
#include <base/strings/string_split.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
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

std::string ConstructOutput() {
  std::string output;
  base::Value result_dict(base::Value::Type::DICTIONARY);
  result_dict.SetIntKey("wearPercentage", 100 - (kHighChargeFull * 100 /
                                                 kFakeBatteryChargeFullDesign));
  result_dict.SetIntKey("cycleCount", kLowCycleCount);
  result_dict.SetStringKey("manufacturer", kFakeManufacturer);
  result_dict.SetIntKey("currentNow", kFakeCurrentNow);
  result_dict.SetIntKey("present", kFakePresent);
  result_dict.SetStringKey("status", kFakeStatus);
  result_dict.SetIntKey("voltageNow", kFakeVoltageNow);
  result_dict.SetIntKey("chargeFull", kHighChargeFull);
  result_dict.SetIntKey("chargeFullDesign", kFakeBatteryChargeFullDesign);
  result_dict.SetIntKey("chargeNow", kFakeChargeNow);
  base::Value output_dict(base::Value::Type::DICTIONARY);
  output_dict.SetKey("resultDetails", std::move(result_dict));
  base::JSONWriter::WriteWithOptions(
      output_dict, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &output);
  return output;
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

  void WriteFilesReadByLog() {
    WriteFileContents(kBatteryManufacturerFileName, kFakeManufacturer);
    WriteFileContents(kBatteryCurrentNowFileName,
                      std::to_string(kFakeCurrentNow));
    WriteFileContents(kBatteryPresentFileName, std::to_string(kFakePresent));
    WriteFileContents(kBatteryStatusFileName, kFakeStatus);
    WriteFileContents(kBatteryVoltageNowFileName,
                      std::to_string(kFakeVoltageNow));
    WriteFileContents(kBatteryChargeNowFileName,
                      std::to_string(kFakeChargeNow));
  }

  void WriteFileContents(const std::string& relative_file_path,
                         const std::string& file_contents) {
    EXPECT_TRUE(
        WriteFileAndCreateParentDirs(temp_dir_path()
                                         .AppendASCII(kBatteryDirectoryPath)
                                         .AppendASCII(relative_file_path),
                                     file_contents));
  }

  const base::FilePath& temp_dir_path() const {
    return mock_context_.root_dir();
  }

 private:
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that the battery health routine fails if the cycle count is too high.
TEST_F(BatteryHealthRoutineTest, HighCycleCount) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatteryCycleCountFileName,
                    std::to_string(kHighCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatteryHealthExcessiveCycleCountMessage);
}

// Test that the battery health routine fails if cycle_count is not present.
TEST_F(BatteryHealthRoutineTest, NoCycleCount) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryHealthFailedReadingCycleCountMessage);
}

// Test that the battery health routine fails if the wear percentage is too
// high.
TEST_F(BatteryHealthRoutineTest, HighWearPercentage) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName, std::to_string(kLowChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatteryCycleCountFileName, std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatteryHealthExcessiveWearMessage);
}

// Test that the battery health routine fails if neither charge_full nor
// energy_full are present.
TEST_F(BatteryHealthRoutineTest, NoWearPercentage) {
  CreateRoutine();
  WriteFileContents(kBatteryCycleCountFileName, std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kError,
      kBatteryHealthFailedCalculatingWearPercentageMessage);
}

// Test that the battery health routine passes if the cycle count and wear
// percentage are within acceptable limits.
TEST_F(BatteryHealthRoutineTest, GoodParameters) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatteryCycleCountFileName, std::to_string(kLowCycleCount));
  WriteFilesReadByLog();
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

// Test that the battery health routine will find energy-reporting batteries.
TEST_F(BatteryHealthRoutineTest, EnergyReportingBattery) {
  CreateRoutine();
  WriteFileContents(kBatteryEnergyFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryEnergyFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  WriteFileContents(kBatteryCycleCountFileName, std::to_string(kLowCycleCount));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryHealthRoutinePassedMessage);
}

// Test that the battery health routine uses the expected full path to
// cycle_count, relative to the temporary test directory.
TEST_F(BatteryHealthRoutineTest, FullCycleCountPath) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  EXPECT_TRUE(
      WriteFileAndCreateParentDirs(temp_dir_path()
                                       .AppendASCII(kBatteryDirectoryPath)
                                       .AppendASCII(kBatteryCycleCountFileName),
                                   std::to_string(kLowCycleCount)));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryHealthRoutinePassedMessage);
}

// Test that the battery health routine catches invalid parameters.
TEST_F(BatteryHealthRoutineTest, InvalidParameters) {
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
  // When the capacity exceeds the design capacity, the battery shouldn't be
  // worn at all.
  constexpr int kNotWornPercentage = 0;
  CreateRoutine(kMaximumCycleCount, kNotWornPercentage);
  // Set the capacity to anything higher than the design capacity.
  constexpr int kHigherCapacity = 100;
  constexpr int kLowerDesignCapacity = 20;
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHigherCapacity));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kLowerDesignCapacity));
  EXPECT_TRUE(
      WriteFileAndCreateParentDirs(temp_dir_path()
                                       .AppendASCII(kBatteryDirectoryPath)
                                       .AppendASCII(kBatteryCycleCountFileName),
                                   std::to_string(kLowCycleCount)));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryHealthRoutinePassedMessage);
}

// Test that the battery health routine fails when invalid file contents are
// read.
TEST_F(BatteryHealthRoutineTest, InvalidFileContents) {
  CreateRoutine();
  WriteFileContents(kBatteryChargeFullFileName,
                    std::to_string(kHighChargeFull));
  WriteFileContents(kBatteryChargeFullDesignFileName,
                    std::to_string(kFakeBatteryChargeFullDesign));
  constexpr char kInvalidUnsignedInt[] = "Invalid unsigned int!";
  EXPECT_TRUE(
      WriteFileAndCreateParentDirs(temp_dir_path()
                                       .AppendASCII(kBatteryDirectoryPath)
                                       .AppendASCII(kBatteryCycleCountFileName),
                                   kInvalidUnsignedInt));
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryHealthFailedReadingCycleCountMessage);
}

}  // namespace diagnostics
