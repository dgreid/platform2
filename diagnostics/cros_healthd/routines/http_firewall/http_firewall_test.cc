// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/run_loop.h>
#include <base/optional.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/routines/http_firewall/http_firewall.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

using testing::_;
using testing::Invoke;
using testing::StrictMock;
using testing::Values;
using testing::WithArg;
using testing::WithParamInterface;

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// POD struct for HttpFirewallProblemTest.
struct HttpFirewallProblemTestParams {
  network_diagnostics_ipc::HttpFirewallProblem problem_enum;
  std::string failure_message;
};

}  // namespace

class HttpFirewallRoutineTest : public testing::Test {
 protected:
  HttpFirewallRoutineTest() = default;
  HttpFirewallRoutineTest(const HttpFirewallRoutineTest&) = delete;
  HttpFirewallRoutineTest& operator=(const HttpFirewallRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    routine_ = CreateHttpFirewallRoutine(network_diagnostics_adapter());
  }

  mojo_ipc::RoutineUpdatePtr RunRoutineAndWaitForExit() {
    DCHECK(routine_);
    mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    routine_->Start();
    routine_->PopulateStatusUpdate(&update, true);
    return chromeos::cros_healthd::mojom::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union));
  }

  MockNetworkDiagnosticsAdapter* network_diagnostics_adapter() {
    return mock_context_.network_diagnostics_adapter();
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  MockContext mock_context_;
  std::unique_ptr<DiagnosticRoutine> routine_;
};

// Test that the HttpFirewall routine can be run successfully.
TEST_F(HttpFirewallRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpFirewallCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            /*problems=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kHttpFirewallRoutineNoProblemMessage);
}

// Test that the HttpFirewall routine returns a kNotRun status when it is not
// run.
TEST_F(HttpFirewallRoutineTest, RoutineNotRun) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpFirewallCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            /*problem=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kHttpFirewallRoutineNotRunMessage);
}

// Tests that the HttpFirewall routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the HttpFirewallProblemTestParams POD struct):
// * |problem_enum| - The type of HttpFirewall problem.
// * |failure_message| - Failure message for a problem.
class HttpFirewallProblemTest
    : public HttpFirewallRoutineTest,
      public WithParamInterface<HttpFirewallProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  HttpFirewallProblemTestParams params() const { return GetParam(); }
};

// Test that the HttpFirewall routine handles the given HTTP firewall problem.
TEST_P(HttpFirewallProblemTest, HandleHttpFirewallProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpFirewallCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kProblem,
            {params().problem_enum});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kFailed,
                             params().failure_message);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    HttpFirewallProblemTest,
    Values(
        HttpFirewallProblemTestParams{
            network_diagnostics_ipc::HttpFirewallProblem::
                kDnsResolutionFailuresAboveThreshold,
            kHttpFirewallRoutineHighDnsResolutionFailureRateProblemMessage},
        HttpFirewallProblemTestParams{
            network_diagnostics_ipc::HttpFirewallProblem::kFirewallDetected,
            kHttpFirewallRoutineFirewallDetectedProblemMessage},
        HttpFirewallProblemTestParams{
            network_diagnostics_ipc::HttpFirewallProblem::kPotentialFirewall,
            kHttpFirewallRoutinePotentialFirewallProblemMessage}));

}  // namespace diagnostics
