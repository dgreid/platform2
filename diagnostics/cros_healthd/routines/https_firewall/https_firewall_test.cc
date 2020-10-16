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

#include "diagnostics/cros_healthd/routines/https_firewall/https_firewall.h"
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

// POD struct for HttpsFirewallProblemTest.
struct HttpsFirewallProblemTestParams {
  network_diagnostics_ipc::HttpsFirewallProblem problem_enum;
  std::string failure_message;
};

}  // namespace

class HttpsFirewallRoutineTest : public testing::Test {
 protected:
  HttpsFirewallRoutineTest() = default;
  HttpsFirewallRoutineTest(const HttpsFirewallRoutineTest&) = delete;
  HttpsFirewallRoutineTest& operator=(const HttpsFirewallRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    routine_ = CreateHttpsFirewallRoutine(network_diagnostics_adapter());
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

// Test that the HttpsFirewall routine can be run successfully.
TEST_F(HttpsFirewallRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpsFirewallCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            /*problems=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kHttpsFirewallRoutineNoProblemMessage);
}

// Test that the HttpsFirewall routine returns an error when it is not
// run.
TEST_F(HttpsFirewallRoutineTest, RoutineError) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpsFirewallCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            /*problem=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun,
                             kHttpsFirewallRoutineNotRunMessage);
}

// Tests that the HttpsFirewall routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the HttpsFirewallProblemTestParams POD struct):
// * |problem_enum| - The type of HttpsFirewall problem.
// * |failure_message| - Failure message for a problem.
class HttpsFirewallProblemTest
    : public HttpsFirewallRoutineTest,
      public WithParamInterface<HttpsFirewallProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  HttpsFirewallProblemTestParams params() const { return GetParam(); }
};

// Test that the HttpsFirewall routine handles the given HTTPS firewall problem.
TEST_P(HttpsFirewallProblemTest, HandleHttpsFirewallProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunHttpsFirewallRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               HttpsFirewallCallback callback) {
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
    HttpsFirewallProblemTest,
    Values(
        HttpsFirewallProblemTestParams{
            network_diagnostics_ipc::HttpsFirewallProblem::
                kHighDnsResolutionFailureRate,
            kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage},
        HttpsFirewallProblemTestParams{
            network_diagnostics_ipc::HttpsFirewallProblem::kFirewallDetected,
            kHttpsFirewallRoutineFirewallDetectedProblemMessage},
        HttpsFirewallProblemTestParams{
            network_diagnostics_ipc::HttpsFirewallProblem::kPotentialFirewall,
            kHttpsFirewallRoutinePotentialFirewallProblemMessage}));

}  // namespace diagnostics
