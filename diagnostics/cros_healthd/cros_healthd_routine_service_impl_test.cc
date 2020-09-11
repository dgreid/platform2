// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/common/system/mock_debugd_adapter.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service_impl.h"
#include "diagnostics/cros_healthd/fake_cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

using testing::StrictMock;

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

std::set<mojo_ipc::DiagnosticRoutineEnum> GetAllAvailableRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth,
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck,
      mojo_ipc::DiagnosticRoutineEnum::kAcPower,
      mojo_ipc::DiagnosticRoutineEnum::kCpuCache,
      mojo_ipc::DiagnosticRoutineEnum::kCpuStress,
      mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel,
      mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest,
      mojo_ipc::DiagnosticRoutineEnum::kDiskRead,
      mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge,
      mojo_ipc::DiagnosticRoutineEnum::kMemory};
}

std::set<mojo_ipc::DiagnosticRoutineEnum> GetBatteryRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth,
      mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge};
}

std::set<mojo_ipc::DiagnosticRoutineEnum> GetNvmeRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel,
      mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest};
}

std::set<mojo_ipc::DiagnosticRoutineEnum> GetWilcoRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel};
}

std::set<mojo_ipc::DiagnosticRoutineEnum> GetSmartCtlRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck};
}

std::set<mojo_ipc::DiagnosticRoutineEnum> GetFioRoutines() {
  return std::set<mojo_ipc::DiagnosticRoutineEnum>{
      mojo_ipc::DiagnosticRoutineEnum::kDiskRead};
}

}  // namespace

// Tests for the CrosHealthdRoutineServiceImpl class.
class CrosHealthdRoutineServiceImplTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    mock_context_.fake_system_config()->SetFioSupported(true);
    mock_context_.fake_system_config()->SetHasBattery(true);
    mock_context_.fake_system_config()->SetNvmeSupported(true);
    mock_context_.fake_system_config()->SetSmartCtrlSupported(true);
    mock_context_.fake_system_config()->SetIsWilcoDevice(true);

    CreateService();
  }

  // The service needs to be recreated anytime the underlying conditions for
  // which tests are populated change.
  void CreateService() {
    service_ = std::make_unique<CrosHealthdRoutineServiceImpl>(
        &mock_context_, &routine_factory_);
  }

  CrosHealthdRoutineServiceImpl* service() { return service_.get(); }

  FakeCrosHealthdRoutineFactory* routine_factory() { return &routine_factory_; }

  MockContext* mock_context() { return &mock_context_; }

  mojo_ipc::RoutineUpdatePtr ExecuteGetRoutineUpdate(
      int32_t id,
      mojo_ipc::DiagnosticRoutineCommandEnum command,
      bool include_output) {
    mojo_ipc::RoutineUpdate update{/*progress_percent=*/0, mojo::ScopedHandle(),
                                   mojo_ipc::RoutineUpdateUnion::New()};
    service_->GetRoutineUpdate(id, command, include_output, &update);
    return mojo_ipc::RoutineUpdate::New(update.progress_percent,
                                        std::move(update.output),
                                        std::move(update.routine_update_union));
  }

 private:
  FakeCrosHealthdRoutineFactory routine_factory_;
  MockContext mock_context_;
  std::unique_ptr<CrosHealthdRoutineServiceImpl> service_;
};

// Test that GetAvailableRoutines() returns the expected list of routines when
// all routines are supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutines) {
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());
  EXPECT_EQ(reply_set, GetAllAvailableRoutines());
}

// Test that GetAvailableRoutines returns the expected list of routines when
// battery routines are not supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutinesNoBattery) {
  mock_context()->fake_system_config()->SetHasBattery(false);
  CreateService();
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());
  auto expected_routines = GetAllAvailableRoutines();
  for (auto r : GetBatteryRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// NVME routines are not supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutinesNoNvme) {
  mock_context()->fake_system_config()->SetNvmeSupported(false);
  CreateService();
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetNvmeRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// Smartctl routines are not supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutinesNoSmartctl) {
  mock_context()->fake_system_config()->SetSmartCtrlSupported(false);
  CreateService();
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetSmartCtlRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// fio routines are not supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutinesNoFio) {
  mock_context()->fake_system_config()->SetFioSupported(false);
  CreateService();
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetFioRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// wilco routines are not supported.
TEST_F(CrosHealthdRoutineServiceImplTest, GetAvailableRoutinesNotWilcoDevice) {
  mock_context()->fake_system_config()->SetIsWilcoDevice(false);
  CreateService();
  auto reply = service()->GetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetWilcoRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
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

// Test that the floating point accuracy routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunFloatingPointAccuracyRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunFloatingPointAccuracyRoutine(
      /*exec_duration=*/base::TimeDelta::FromSeconds(120), &response.id,
      &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the NVMe wear level routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunNvmeWearLevelRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunNvmeWearLevelRoutine(
      /*wear_level_threshold=*/30, &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the nvme self-test routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunNvmeSelfTestRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunNvmeSelfTestRoutine(
      /*nvme_self_test_type=*/mojo_ipc::NvmeSelfTestTypeEnum::kShortSelfTest,
      &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the disk read routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunDiskReadRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(10);
  service()->RunDiskReadRoutine(
      /*type*/ mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead,
      /*exec_duration=*/exec_duration, /*file_size_mb=*/1024, &response.id,
      &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the prime search routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunPrimeSearchRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(10);
  service()->RunPrimeSearchRoutine(
      /*exec_duration=*/exec_duration, /*max_num=*/1000000, &response.id,
      &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the battery discharge routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunBatteryDischargeRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  // TODO(crbug/1065463): Treat this as an interactive routine.
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunBatteryDischargeRoutine(
      /*exec_duration=*/base::TimeDelta::FromSeconds(23),
      /*maximum_discharge_percent_allowed=*/78, &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the battery charge routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunBatteryChargeRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  // TODO(crbug/1065463): Treat this as an interactive routine.
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunBatteryChargeRoutine(
      /*exec_duration=*/base::TimeDelta::FromSeconds(54),
      /*minimum_charge_percent_required=*/56, &response.id, &response.status);
  EXPECT_EQ(response.id, 1);
  EXPECT_EQ(response.status, kExpectedStatus);
}

// Test that the memory routine can be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunMemoryRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunMemoryRoutine(&response.id, &response.status);
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

// Test that an unsupported routine cannot be run.
TEST_F(CrosHealthdRoutineServiceImplTest, RunUnsupportedRoutine) {
  mock_context()->fake_system_config()->SetSmartCtrlSupported(false);
  CreateService();
  routine_factory()->SetNonInteractiveStatus(
      mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported,
      /*status_message=*/"", /*progress_percent=*/0,
      /*output=*/"");
  mojo_ipc::RunRoutineResponse response;
  service()->RunSmartctlCheckRoutine(&response.id, &response.status);
  EXPECT_EQ(response.id, mojo_ipc::kFailedToStartId);
  EXPECT_EQ(response.status,
            mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported);
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
