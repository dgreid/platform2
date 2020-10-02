// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/task/post_task.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/system/handle.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/routines/memory/memory.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/cros_healthd_executor.mojom.h"

using testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = chromeos::cros_healthd::mojom;

// Location of files containing test data (fake memtester output).
constexpr char kTestDataRoot[] = "cros_healthd/routines/memory/testdata";

}  // namespace

class MemoryRoutineTest : public testing::Test {
 protected:
  MemoryRoutineTest() = default;
  MemoryRoutineTest(const MemoryRoutineTest&) = delete;
  MemoryRoutineTest& operator=(const MemoryRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    routine_ = std::make_unique<MemoryRoutine>(
        &mock_context_, task_environment_.GetMockTickClock());
  }

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojo_ipc::RoutineUpdate* update() { return &update_; }

  void FastForwardBy(base::TimeDelta time) {
    task_environment_.FastForwardBy(time);
  }

  void RunRoutineAndWaitForExit() {
    routine_->Start();

    // Since the memory routine has finished by the time Start() returns, there
    // is no need to wait.
    routine_->PopulateStatusUpdate(&update_, true);
  }

  mojo_ipc::RoutineUpdatePtr GetUpdate() {
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  void SetExecutorResponse(int32_t exit_code,
                           const base::Optional<std::string>& outfile_name,
                           const base::Optional<base::TimeDelta>& delay) {
    EXPECT_CALL(*mock_context_.mock_executor(), RunMemtester(_))
        .WillOnce(WithArg<0>(
            Invoke([=](executor_ipc::Executor::RunMemtesterCallback callback) {
              executor_ipc::ProcessResult result;
              result.return_code = exit_code;
              if (outfile_name.has_value()) {
                EXPECT_TRUE(base::ReadFileToString(
                    base::FilePath(kTestDataRoot).Append(outfile_name.value()),
                    &result.out));
              }
              if (!delay.has_value()) {
                std::move(callback).Run(result.Clone());
                return;
              }

              base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
                  FROM_HERE,
                  base::BindOnce(
                      [](executor_ipc::Executor::RunMemtesterCallback callback,
                         executor_ipc::ProcessResultPtr result) {
                        LOG(ERROR) << "I am here!";
                        std::move(callback).Run(std::move(result));
                        LOG(ERROR) << "But not here!";
                      },
                      std::move(callback), result.Clone()),
                  delay.value());
            })));
  }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
  mojo_ipc::RoutineUpdate update_{0, mojo::ScopedHandle(),
                                  mojo_ipc::RoutineUpdateUnion::New()};
};

// Test that we can create a memory routine with the default tick clock.
TEST_F(MemoryRoutineTest, DefaultTickClock) {
  MockContext mock_context;
  ASSERT_TRUE(mock_context.Initialize());

  MemoryRoutine routine(&mock_context);

  EXPECT_EQ(routine.GetStatus(), mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

// Test that the memory routine can run successfully.
TEST_F(MemoryRoutineTest, RoutineSuccess) {
  SetExecutorResponse(EXIT_SUCCESS, "good_memtester_output",
                      base::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kMemoryRoutineSucceededMessage);
}

// Test that the memory routine handles the memtester binary failing to run.
TEST_F(MemoryRoutineTest, MemtesterBinaryFailsToRun) {
  SetExecutorResponse(EXIT_FAILURE, base::nullopt /* outfile_name */,
                      base::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(
      update()->routine_update_union,
      mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
      kMemoryRoutineAllocatingLockingInvokingFailureMessage);
}

// Test that the memory routine handles a stuck address failure.
TEST_F(MemoryRoutineTest, StuckAddressFailure) {
  SetExecutorResponse(MemtesterErrorCodes::kStuckAddressTestError,
                      base::nullopt /* outfile_name */,
                      base::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kMemoryRoutineStuckAddressTestFailureMessage);
}

// Test that the memory routine handles a test failure other than stuck address.
TEST_F(MemoryRoutineTest, OtherTestFailure) {
  SetExecutorResponse(MemtesterErrorCodes::kOtherTestError,
                      base::nullopt /* outfile_name */,
                      base::nullopt /* delay */);

  RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(update()->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kMemoryRoutineOtherTestFailureMessage);
}

// Test that calling resume doesn't crash.
TEST_F(MemoryRoutineTest, Resume) {
  routine()->Resume();
}

// Test that calling cancel doesn't crash.
TEST_F(MemoryRoutineTest, Cancel) {
  routine()->Cancel();
}

}  // namespace diagnostics
