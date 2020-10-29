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

#include "diagnostics/cros_healthd/routines/dns_resolver_present/dns_resolver_present.h"
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

// POD struct for DnsResolverPresentProblemTest.
struct DnsResolverPresentProblemTestParams {
  network_diagnostics_ipc::DnsResolverPresentProblem problem_enum;
  std::string failure_message;
};

}  // namespace

class DnsResolverPresentRoutineTest : public testing::Test {
 protected:
  DnsResolverPresentRoutineTest() = default;
  DnsResolverPresentRoutineTest(const DnsResolverPresentRoutineTest&) = delete;
  DnsResolverPresentRoutineTest& operator=(
      const DnsResolverPresentRoutineTest&) = delete;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    routine_ = CreateDnsResolverPresentRoutine(network_diagnostics_adapter());
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

// Test that the DnsResolverPresent routine can be run successfully.
TEST_F(DnsResolverPresentRoutineTest, RoutineSuccess) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolverPresentRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               DnsResolverPresentCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNoProblem,
            /*problems=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kPassed,
                             kDnsResolverPresentRoutineNoProblemMessage);
}

// Test that the DnsResolverPresent routine returns an error when it is not
// run.
TEST_F(DnsResolverPresentRoutineTest, RoutineError) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolverPresentRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               DnsResolverPresentCallback callback) {
        std::move(callback).Run(
            network_diagnostics_ipc::RoutineVerdict::kNotRun,
            /*problem=*/{});
      }));

  mojo_ipc::RoutineUpdatePtr routine_update = RunRoutineAndWaitForExit();
  VerifyNonInteractiveUpdate(routine_update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kDnsResolverPresentRoutineNotRunMessage);
}

// Tests that the DnsResolverPresent routine handles problems.
//
// This is a parameterized test with the following parameters (accessed through
// the DnsResolverPresentProblemTestParams POD struct):
// * |problem_enum| - The type of DnsResolverPresent problem.
// * |failure_message| - Failure message for a problem.
class DnsResolverPresentProblemTest
    : public DnsResolverPresentRoutineTest,
      public WithParamInterface<DnsResolverPresentProblemTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  DnsResolverPresentProblemTestParams params() const { return GetParam(); }
};

// Test that the DnsResolverPresent routine handles the given DNS resolver
// present problem.
TEST_P(DnsResolverPresentProblemTest, HandleDnsResolverPresentProblem) {
  EXPECT_CALL(*(network_diagnostics_adapter()), RunDnsResolverPresentRoutine(_))
      .WillOnce(Invoke([&](network_diagnostics_ipc::NetworkDiagnosticsRoutines::
                               DnsResolverPresentCallback callback) {
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
    DnsResolverPresentProblemTest,
    Values(
        DnsResolverPresentProblemTestParams{
            network_diagnostics_ipc::DnsResolverPresentProblem::
                kNoNameServersFound,
            kDnsResolverPresentRoutineNoNameServersFoundProblemMessage},
        DnsResolverPresentProblemTestParams{
            network_diagnostics_ipc::DnsResolverPresentProblem::
                kMalformedNameServers,
            kDnsResolverPresentRoutineMalformedNameServersProblemMessage},
        DnsResolverPresentProblemTestParams{
            network_diagnostics_ipc::DnsResolverPresentProblem::
                kEmptyNameServers,
            kDnsResolverPresentRoutineEmptyNameServersProblemMessage}));

}  // namespace diagnostics
