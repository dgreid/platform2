// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Test data.
constexpr auto kExpectedStatus = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
constexpr char kExpectedStatusMessage[] = "This is a status message!";
constexpr char kExpectedOutput[] = "This is output!";

// POD struct for ReportProgressPercentTest.
struct ReportProgressPercentTestParams {
  mojo_ipc::DiagnosticRoutineStatusEnum status;
  uint32_t expected_progress_percent;
};

// Task for a SimpleRoutine to run. Does no work other than setting
// |status_out|, |status_message_out| and |output_out|.
void FakeRoutineTask(mojo_ipc::DiagnosticRoutineStatusEnum status_in,
                     const std::string& status_message_in,
                     const std::string& output_in,
                     mojo_ipc::DiagnosticRoutineStatusEnum* status_out,
                     std::string* status_message_out,
                     std::string* output_out) {
  DCHECK(status_out);
  DCHECK(status_message_out);
  DCHECK(output_out);

  *status_out = status_in;
  *status_message_out = std::move(status_message_in);
  *output_out = std::move(output_in);
}

}  // namespace

class SimpleRoutineTest : public testing::Test {
 protected:
  SimpleRoutineTest() = default;
  SimpleRoutineTest(const SimpleRoutineTest&) = delete;
  SimpleRoutineTest& operator=(const SimpleRoutineTest&) = delete;

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojo_ipc::RoutineUpdate* update() { return &update_; }

  void CreateRoutine(mojo_ipc::DiagnosticRoutineStatusEnum desired_status =
                         mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                     const std::string& desired_status_message = "",
                     const std::string& desired_output = "") {
    routine_ = std::make_unique<SimpleRoutine>(base::BindOnce(
        &FakeRoutineTask, desired_status, std::move(desired_status_message),
        std::move(desired_output)));
  }

  void RunRoutineAndCollectUpdate(bool include_output) {
    routine_->Start();

    // Since the SimpleRoutine has finished by the time Start() returns, there
    // is no need to wait.
    routine_->PopulateStatusUpdate(&update_, include_output);
  }

 private:
  std::unique_ptr<SimpleRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that we can run a noninteractive routine and retrieve its status update.
TEST_F(SimpleRoutineTest, RunAndRetrieveStatusUpdate) {
  CreateRoutine(kExpectedStatus, kExpectedStatusMessage, kExpectedOutput);

  RunRoutineAndCollectUpdate(/*include_output=*/true);

  VerifyNonInteractiveUpdate(update()->routine_update_union, kExpectedStatus,
                             kExpectedStatusMessage);
  EXPECT_EQ(GetStringFromMojoHandle(std::move(update()->output)),
            kExpectedOutput);
  EXPECT_EQ(update()->progress_percent, 100);
}

// Test that retrieving a status update with the include_output flag set to
// false doesn't return any output.
TEST_F(SimpleRoutineTest, NoOutputReturned) {
  CreateRoutine(kExpectedStatus, kExpectedStatusMessage, kExpectedOutput);

  RunRoutineAndCollectUpdate(/*include_output=*/false);

  VerifyNonInteractiveUpdate(update()->routine_update_union, kExpectedStatus,
                             kExpectedStatusMessage);
  EXPECT_TRUE(GetStringFromMojoHandle(std::move(update()->output)).empty());
  EXPECT_EQ(update()->progress_percent, 100);
}

// Test that calling resume doesn't crash.
TEST_F(SimpleRoutineTest, Resume) {
  CreateRoutine();

  routine()->Resume();
}

// Test that calling cancel doesn't crash.
TEST_F(SimpleRoutineTest, Cancel) {
  CreateRoutine();

  routine()->Cancel();
}

// Test that we can retrieve the status of a simple routine.
TEST_F(SimpleRoutineTest, GetStatus) {
  CreateRoutine();

  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

// Tests that progress is reported correctly for each possible status.
//
// This is a parameterized test with the following parameters (accessed
// through the ReportProgressPercentTestParams POD struct):
// * |status| - status reported by the routine's task.
// * |expected_progress_percent| - expected value for the routine's progress
//                                 percent.
class ReportProgressPercentTest
    : public SimpleRoutineTest,
      public testing::WithParamInterface<ReportProgressPercentTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ReportProgressPercentTestParams params() const { return GetParam(); }
};

// Test that we can parse the given uname response for CPU architecture.
TEST_P(ReportProgressPercentTest, ReportProgressPercent) {
  CreateRoutine(params().status, kExpectedStatusMessage, kExpectedOutput);

  RunRoutineAndCollectUpdate(/*include_output=*/true);

  VerifyNonInteractiveUpdate(update()->routine_update_union, params().status,
                             kExpectedStatusMessage);
  EXPECT_EQ(GetStringFromMojoHandle(std::move(update()->output)),
            kExpectedOutput);
  EXPECT_EQ(update()->progress_percent, params().expected_progress_percent);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ReportProgressPercentTest,
    testing::Values(
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kPassed, 100},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kFailed, 100},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kError, 100},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling, 0},
        ReportProgressPercentTestParams{
            mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported, 0}));

}  // namespace diagnostics
