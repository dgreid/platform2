// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/routines/battery/battery.h"
#include "diagnostics/routines/routine_test_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
constexpr uint32_t kLowmAh = 1000;
constexpr uint32_t kHighmAh = 10000;
constexpr uint32_t kGoodFileContents = 8948000;
constexpr uint32_t kBadFileContents = 10;

std::string FakeGoodFileContents() {
  return std::to_string(kGoodFileContents);
}

std::string FakeBadFileContents() {
  return std::to_string(kBadFileContents);
}

}  // namespace

class BatteryRoutineTest : public testing::Test {
 protected:
  BatteryRoutineTest() = default;
  BatteryRoutineTest(const BatteryRoutineTest&) = delete;
  BatteryRoutineTest& operator=(const BatteryRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  }

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojo_ipc::RoutineUpdate* update() { return &update_; }

  void CreateRoutine(uint32_t low_mah = kLowmAh, uint32_t high_mah = kHighmAh) {
    routine_ = std::make_unique<BatteryRoutine>(low_mah, high_mah);
    routine_->set_root_dir_for_testing(temp_dir_.GetPath());
  }

  void RunRoutineAndWaitForExit() {
    routine_->Start();

    // Since the BatteryRoutine has finished by the time Start() returns, there
    // is no need to wait.
    routine_->PopulateStatusUpdate(&update_, true);
  }

  void WriteChargeFullDesign(const std::string& file_contents) {
    EXPECT_TRUE(WriteFileAndCreateParentDirs(
        temp_dir_path().Append(base::FilePath(kBatteryChargeFullDesignPath)),
        file_contents));
  }

  const base::FilePath& temp_dir_path() const { return temp_dir_.GetPath(); }

 private:
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<BatteryRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that the battery routine fails if charge_full_design does not exist.
TEST_F(BatteryRoutineTest, NoChargeFullDesign) {
  CreateRoutine();
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryFailedReadingChargeFullDesignMessage);
}

// Test that the battery routine fails if charge_full_design is outside the
// limits.
TEST_F(BatteryRoutineTest, LowChargeFullDesign) {
  CreateRoutine();
  WriteChargeFullDesign(FakeBadFileContents());
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kBatteryRoutineFailedMessage);
}

// Test that the battery routine passes if charge_full_design is within the
// limits.
TEST_F(BatteryRoutineTest, GoodChargeFullDesign) {
  CreateRoutine();
  WriteChargeFullDesign(FakeGoodFileContents());
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kBatteryRoutineSucceededMessage);
}

// Test that the battery routine handles invalid charge_full_design contents.
TEST_F(BatteryRoutineTest, InvalidChargeFullDesign) {
  CreateRoutine();
  constexpr char kInvalidChargeFullDesign[] = "Not an unsigned int!";
  WriteChargeFullDesign(kInvalidChargeFullDesign);
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryFailedParsingChargeFullDesignMessage);
}

// Test that the battery routine handles invalid parameters.
TEST_F(BatteryRoutineTest, InvalidParameters) {
  constexpr uint32_t kInvalidLowMah = 5;
  constexpr uint32_t kInvalidHighMah = 4;
  CreateRoutine(kInvalidLowMah, kInvalidHighMah);
  RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kBatteryRoutineParametersInvalidMessage);
}

// Test that calling resume doesn't crash.
TEST_F(BatteryRoutineTest, Resume) {
  CreateRoutine();
  routine()->Resume();
}

// Test that calling cancel doesn't crash.
TEST_F(BatteryRoutineTest, Cancel) {
  CreateRoutine();
  routine()->Cancel();
}

// Test that we can retrieve the status of the battery routine.
TEST_F(BatteryRoutineTest, GetStatus) {
  CreateRoutine();
  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

}  // namespace diagnostics
