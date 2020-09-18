// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/run_loop.h>
#include <base/optional.h>
#include <base/message_loop/message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/lan_connectivity/lan_connectivity.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::WithArg;

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

}  // namespace

class LanConnectivityRoutineTest : public testing::Test {
 protected:
  LanConnectivityRoutineTest() = default;
  LanConnectivityRoutineTest(const LanConnectivityRoutineTest&) = delete;
  LanConnectivityRoutineTest& operator=(const LanConnectivityRoutineTest&) =
      delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    routine_ =
        std::make_unique<LanConnectivityRoutine>(network_diagnostics_adapter());
  }

  DiagnosticRoutine* routine() { return routine_.get(); }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return mojo_ipc::RoutineUpdate::New(update.progress_percent,
                                        std::move(update.output),
                                        std::move(update.routine_update_union));
  }

  MockNetworkDiagnosticsAdapter* network_diagnostics_adapter() {
    return mock_context_.network_diagnostics_adapter();
  }

 private:
  base::MessageLoop message_loop_;
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the LanConnectivity routine is in the ready state when created.
TEST_F(LanConnectivityRoutineTest, RoutineReady) {
  EXPECT_EQ(routine()->GetStatus(),
            mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
}

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kPassed when the the
// verdict is network_diagnostics::mojom::RoutineVerdict::kNoProblem.
TEST_F(LanConnectivityRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunLanConnectivityRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               LanConnectivityCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem);
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kLanConnectivityRoutineNoProblemMessage);
}

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kFailed when the verdict is
// network_diagnostics::mojom::RoutineVerdict::kProblem.
TEST_F(LanConnectivityRoutineTest, RoutineFailed) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunLanConnectivityRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               LanConnectivityCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kProblem);
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             kLanConnectivityRoutineProblemMessage);
}

// Test that the LanConnectivity routine returns
// cros_healthd::mojom::DiagnosticRoutineStatusEnum::kError when the routine is
// a network_diagnostics::mojom::RoutineVerdict::kNotRun.
TEST_F(LanConnectivityRoutineTest, RoutineError) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunLanConnectivityRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               LanConnectivityCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNotRun);
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();

  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kLanConnectivityRoutineNotRunMessage);
}

}  // namespace diagnostics
