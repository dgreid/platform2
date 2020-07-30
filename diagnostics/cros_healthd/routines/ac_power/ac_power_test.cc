// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/optional.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/routines/ac_power/ac_power.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

constexpr char kExpectedPowerType[] = "USB_PD";
constexpr char kPowerSupplyDirectoryPath[] =
    "sys/class/power_supply/foo_power_supply";

}  // namespace

class AcPowerRoutineTest : public testing::Test {
 protected:
  AcPowerRoutineTest() = default;
  AcPowerRoutineTest(const AcPowerRoutineTest&) = delete;
  AcPowerRoutineTest& operator=(const AcPowerRoutineTest&) = delete;

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

  DiagnosticRoutine* routine() { return routine_.get(); }

  void CreateRoutine(mojo_ipc::AcPowerStatusEnum expected_status,
                     const base::Optional<std::string>& expected_power_type) {
    routine_ = std::make_unique<AcPowerRoutine>(
        expected_status, expected_power_type, temp_dir_.GetPath());
  }

  mojo_ipc::RoutineUpdatePtr GetUpdate() {
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  void WriteOnlineFileContents(const std::string& file_contents) {
    EXPECT_TRUE(
        WriteFileAndCreateParentDirs(temp_dir_.GetPath()
                                         .AppendASCII(kPowerSupplyDirectoryPath)
                                         .AppendASCII("online"),
                                     file_contents));
  }

  void WriteTypeFileContents(const std::string& file_contents) {
    EXPECT_TRUE(
        WriteFileAndCreateParentDirs(temp_dir_.GetPath()
                                         .AppendASCII(kPowerSupplyDirectoryPath)
                                         .AppendASCII("type"),
                                     file_contents));
  }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<AcPowerRoutine> routine_;
};

// Test that the routine passes when expecting an online power supply.
TEST_F(AcPowerRoutineTest, OnlineExpectedOnlineRead) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kConnected, kExpectedPowerType);
  WriteOnlineFileContents("1");
  WriteTypeFileContents(kExpectedPowerType);

  routine()->Start();
  auto update = GetUpdate();
  VerifyInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);

  routine()->Resume();
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kAcPowerRoutineSucceededMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine passes when expecting an offline power supply.
TEST_F(AcPowerRoutineTest, OfflineExpectedOfflineRead) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kDisconnected, kExpectedPowerType);
  WriteOnlineFileContents("0");
  WriteTypeFileContents(kExpectedPowerType);

  routine()->Start();
  auto update = GetUpdate();
  VerifyInteractiveUpdate(
      update->routine_update_union,
      mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);

  routine()->Resume();
  update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kAcPowerRoutineSucceededMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine fails when reading offline power supplies and expecting
// online power supplies.
TEST_F(AcPowerRoutineTest, OnlineExpectedOfflineRead) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kConnected, kExpectedPowerType);
  WriteOnlineFileContents("0");
  WriteTypeFileContents(kExpectedPowerType);

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kAcPowerRoutineFailedNotOnlineMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine fails when reading online power supplies and expecting
// offline power supplies.
TEST_F(AcPowerRoutineTest, OfflineExpectedOnlineRead) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kDisconnected, kExpectedPowerType);
  WriteOnlineFileContents("1");
  WriteTypeFileContents(kExpectedPowerType);

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kAcPowerRoutineFailedNotOfflineMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that mismatched power_type values causes the routine to fail.
TEST_F(AcPowerRoutineTest, MismatchedPowerTypes) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kConnected, "power_type_1");
  WriteOnlineFileContents("1");
  WriteTypeFileContents("power_type_2");

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kAcPowerRoutineFailedMismatchedPowerTypesMessage);
  EXPECT_EQ(update->progress_percent, 100);
}

// Test that the routine deals with no valid directories found.
TEST_F(AcPowerRoutineTest, NoValidDirectories) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kConnected, base::nullopt);
  WriteOnlineFileContents("0");
  WriteTypeFileContents("Battery");

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kAcPowerRoutineNoValidPowerSupplyMessage);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);
}

// Test that the routine handles a missing online file.
TEST_F(AcPowerRoutineTest, MissingOnlineFile) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kDisconnected, kExpectedPowerType);
  WriteTypeFileContents(kExpectedPowerType);

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kAcPowerRoutineNoValidPowerSupplyMessage);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);
}

// Test that the routine handles a missing type file.
TEST_F(AcPowerRoutineTest, MissingTypeFile) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kDisconnected, kExpectedPowerType);
  WriteOnlineFileContents("0");

  routine()->Start();
  routine()->Resume();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kAcPowerRoutineNoValidPowerSupplyMessage);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);
}

// Test that we can cancel the routine in its waiting state.
TEST_F(AcPowerRoutineTest, CancelWhenWaiting) {
  CreateRoutine(mojo_ipc::AcPowerStatusEnum::kConnected, base::nullopt);

  routine()->Start();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);

  routine()->Cancel();

  auto update = GetUpdate();
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled,
                             kAcPowerRoutineCancelledMessage);
  EXPECT_EQ(update->progress_percent, kAcPowerRoutineWaitingProgressPercent);
}

}  // namespace diagnostics
