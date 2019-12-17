// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service_impl.h"
#include "diagnostics/cros_healthd/fake_cros_healthd_routine_factory.h"
#include "diagnostics/routines/routine_test_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
constexpr char kRoutineDoesNotExistStatusMessage[] =
    "Specified routine does not exist.";

// POD struct for RoutineUpdateCommandTest.
struct RoutineUpdateCommandTestParams {
  mojo_ipc::DiagnosticRoutineCommandEnum command;
  mojo_ipc::DiagnosticRoutineStatusEnum expected_status;
  int num_expected_start_calls;
  int num_expected_resume_calls;
  int num_expected_cancel_calls;
};

}  // namespace

// Tests for the CrosHealthdRoutineServiceImpl class.
class CrosHealthdRoutineServiceImplTest : public testing::Test {
 protected:
  CrosHealthdRoutineServiceImpl* service() { return &service_; }

  FakeCrosHealthdRoutineFactory* routine_factory() { return &routine_factory_; }

  mojo_ipc::RoutineUpdatePtr ExecuteGetRoutineUpdate(
      int32_t id,
      mojo_ipc::DiagnosticRoutineCommandEnum command,
      bool include_output) {
    mojo_ipc::RoutineUpdate update{/*progress_percent=*/0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    service_.GetRoutineUpdate(id, command, include_output, &update);
    return mojo_ipc::RoutineUpdate::New(update.progress_percent,
                                        std::move(update.output),
                                        std::move(update.routine_update_union));
  }

 private:
  FakeCrosHealthdRoutineFactory routine_factory_;
  CrosHealthdRoutineServiceImpl service_{&routine_factory_};
};

// Test that GetAvailableRoutines returns the expected list of routines.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutines) {
  const std::vector<mojo_ipc::DiagnosticRoutineEnum> kAvailableRoutines = {
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth,
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck,
      mojo_ipc::DiagnosticRoutineEnum::kAcPower,
      mojo_ipc::DiagnosticRoutineEnum::kCpuCache,
      mojo_ipc::DiagnosticRoutineEnum::kCpuStress};
  auto reply = service()->GetAvailableRoutines();
  EXPECT_EQ(reply, kAvailableRoutines);
}

// Test that getting the status of a routine that doesn't exist returns an
// error.
TEST_F(CrosHealthdRoutineServiceImplTest, NonExistingStatus) {
  auto update = ExecuteGetRoutineUpdate(
      /*id=*/0, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      /*include_output=*/false);
  EXPECT_EQ(update->progress_percent, 0);
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kRoutineDoesNotExistStatusMessage);
}

// Test that the battery capacity routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunBatteryCapacityRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunBatteryCapacityRoutine(/*low_mah=*/10, /*high_mah=*/20,
                                       &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the battery health routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunBatteryHealthRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunBatteryHealthRoutine(/*maximum_cycle_count=*/2,
                                     /*percent_battery_wear_allowed=*/30,
                                     &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the urandom routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunUrandomRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunUrandomRoutine(/*length_seconds=*/120, &response.id,
                               &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the smartctl check routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunSmartctlCheckRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunSmartctlCheckRoutine(&response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the AC power routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunAcPowerRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunAcPowerRoutine(
      /*expected_status=*/mojo_ipc::AcPowerStatusEnum::kConnected,
      /*expected_power_type=*/base::Optional<std::string>{"power_type"},
      &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the CPU cache routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunCpuCacheRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunCpuCacheRoutine(base::TimeDelta().FromSeconds(10), &response.id,
                                &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the CPU stress routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunCpuStressRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunCpuStressRoutine(base::TimeDelta().FromMinutes(5), &response.id,
                                 &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that after a routine has been removed, we cannot access its data.
TEST_F(CrosHealthdRoutineServiceImplTest, AccessStoppedRoutine) {
  routine_factory()->SetNonInteractiveStatus(
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning, /*status_message=*/"",
      /*progress_percent=*/50, /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunSmartctlCheckRoutine(&response.id, &response.status);
  ExecuteGetRoutineUpdate(response.id,
                          mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
                          /*include_output=*/false);
  auto update = ExecuteGetRoutineUpdate(
      response.id, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      /*include_output=*/true);
  EXPECT_EQ(update->progress_percent, 0);
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kRoutineDoesNotExistStatusMessage);
}

// Tests for the GetRoutineUpdate() method of RoutineService with different
// commands.
//
// This is a parameterized test with the following parameters (accessed
// through the POD RoutineUpdateCommandTestParams POD struct):
// * |command| - mojo_ipc::DiagnosticRoutineCommandEnum sent to the routine
//               service.
// * |num_expected_start_calls| - number of times the underlying routine's
//                                Start() method is expected to be called.
// * |num_expected_resume_calls| - number of times the underlying routine's
//                                 Resume() method is expected to be called.
// * |num_expected_cancel_calls| - number of times the underlying routine's
//                                 Cancel() method is expected to be called.
class RoutineUpdateCommandTest
    : public CrosHealthdRoutineServiceImplTest,
      public testing::WithParamInterface<RoutineUpdateCommandTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():

  RoutineUpdateCommandTestParams params() const { return GetParam(); }
};

// Test that we can send the given command.
TEST_P(RoutineUpdateCommandTest, SendCommand) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  constexpr char kExpectedStatusMessage[] = "Expected status message.";
  constexpr uint32_t kExpectedProgressPercent = 19;
  constexpr char kExpectedOutput[] = "Expected output.";
  routine_factory()->SetRoutineExpectations(params().num_expected_start_calls,
                                            params().num_expected_resume_calls,
                                            params().num_expected_cancel_calls);
  routine_factory()->SetNonInteractiveStatus(kStatus, kExpectedStatusMessage,
                                             kExpectedProgressPercent,
                                             kExpectedOutput);
  mojo_ipc::RunRoutineResponse response;
  service()->RunSmartctlCheckRoutine(&response.id, &response.status);
  auto update = ExecuteGetRoutineUpdate(response.id, params().command,
                                        /*include_output=*/true);
  EXPECT_EQ(update->progress_percent, kExpectedProgressPercent);
  std::string output = GetStringFromMojoHandle(std::move(update->output));
  EXPECT_EQ(output, kExpectedOutput);
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             params().expected_status, kExpectedStatusMessage);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    RoutineUpdateCommandTest,
    testing::Values(
        RoutineUpdateCommandTestParams{
            mojo_ipc::DiagnosticRoutineCommandEnum::kCancel,
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
            /*num_expected_start_calls=*/1,
            /*num_expected_resume_calls=*/0,
            /*num_expected_cancel_calls=*/1},
        RoutineUpdateCommandTestParams{
            mojo_ipc::DiagnosticRoutineCommandEnum::kContinue,
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
            /*num_expected_start_calls=*/1,
            /*num_expected_resume_calls=*/1,
            /*num_expected_cancel_calls=*/0},
        RoutineUpdateCommandTestParams{
            mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
            mojo_ipc::DiagnosticRoutineStatusEnum::kRunning,
            /*num_expected_start_calls=*/1,
            /*num_expected_resume_calls=*/0,
            /*num_expected_cancel_calls=*/0},
        RoutineUpdateCommandTestParams{
            mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
            mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved,
            /*num_expected_start_calls=*/1,
            /*num_expected_resume_calls=*/0,
            /*num_expected_cancel_calls=*/0}));

}  // namespace diagnostics
