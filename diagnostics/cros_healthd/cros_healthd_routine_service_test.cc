// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/run_loop.h>
#include <base/test/bind_test_util.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/fake_cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/routines/routine_test_utils.h"
#include "diagnostics/cros_healthd/system/fake_system_config.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

using testing::StrictMock;

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

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
      mojo_ipc::DiagnosticRoutineEnum::kMemory,
      mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity,
      mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
      mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged,
      mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
      mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent};
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

// Tests for the CrosHealthdRoutineService class.
class CrosHealthdRoutineServiceTest : public testing::Test {
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
    service_ = std::make_unique<CrosHealthdRoutineService>(&mock_context_,
                                                           &routine_factory_);
  }

  CrosHealthdRoutineService* service() { return service_.get(); }

  FakeCrosHealthdRoutineFactory* routine_factory() { return &routine_factory_; }

  MockContext* mock_context() { return &mock_context_; }

  std::vector<mojo_ipc::DiagnosticRoutineEnum> ExecuteGetAvailableRoutines() {
    base::RunLoop run_loop;
    std::vector<mojo_ipc::DiagnosticRoutineEnum> available_routines;
    service()->GetAvailableRoutines(base::BindLambdaForTesting(
        [&](const std::vector<mojo_ipc::DiagnosticRoutineEnum>& response) {
          available_routines = response;
          run_loop.Quit();
        }));

    run_loop.Run();

    return available_routines;
  }

  mojo_ipc::RoutineUpdatePtr ExecuteGetRoutineUpdate(
      int32_t id,
      mojo_ipc::DiagnosticRoutineCommandEnum command,
      bool include_output) {
    base::RunLoop run_loop;
    mojo_ipc::RoutineUpdatePtr update;
    service()->GetRoutineUpdate(
        id, command, include_output,
        base::BindLambdaForTesting([&](mojo_ipc::RoutineUpdatePtr response) {
          update = mojo_ipc::RoutineUpdate::New(
              response->progress_percent, std::move(response->output),
              std::move(response->routine_update_union));
          run_loop.Quit();
        }));

    run_loop.Run();

    return update;
  }

 private:
  base::test::TaskEnvironment task_environment_;
  FakeCrosHealthdRoutineFactory routine_factory_;
  MockContext mock_context_;
  std::unique_ptr<CrosHealthdRoutineService> service_;
};

// Test that GetAvailableRoutines() returns the expected list of routines when
// all routines are supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutines) {
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());
  EXPECT_EQ(reply_set, GetAllAvailableRoutines());
}

// Test that GetAvailableRoutines returns the expected list of routines when
// battery routines are not supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutinesNoBattery) {
  mock_context()->fake_system_config()->SetHasBattery(false);
  CreateService();
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());
  auto expected_routines = GetAllAvailableRoutines();
  for (auto r : GetBatteryRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// NVME routines are not supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutinesNoNvme) {
  mock_context()->fake_system_config()->SetNvmeSupported(false);
  CreateService();
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetNvmeRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// Smartctl routines are not supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutinesNoSmartctl) {
  mock_context()->fake_system_config()->SetSmartCtrlSupported(false);
  CreateService();
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetSmartCtlRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// fio routines are not supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutinesNoFio) {
  mock_context()->fake_system_config()->SetFioSupported(false);
  CreateService();
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetFioRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that GetAvailableRoutines returns the expected list of routines when
// wilco routines are not supported.
TEST_F(CrosHealthdRoutineServiceTest, GetAvailableRoutinesNotWilcoDevice) {
  mock_context()->fake_system_config()->SetIsWilcoDevice(false);
  CreateService();
  auto reply = ExecuteGetAvailableRoutines();
  std::set<mojo_ipc::DiagnosticRoutineEnum> reply_set(reply.begin(),
                                                      reply.end());

  auto expected_routines = GetAllAvailableRoutines();
  for (const auto r : GetWilcoRoutines())
    expected_routines.erase(r);

  EXPECT_EQ(reply_set, expected_routines);
}

// Test that getting the status of a routine that doesn't exist returns an
// error.
TEST_F(CrosHealthdRoutineServiceTest, NonExistingStatus) {
  auto update = ExecuteGetRoutineUpdate(
      /*id=*/0, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      /*include_output=*/false);
  EXPECT_EQ(update->progress_percent, 0);
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kRoutineDoesNotExistStatusMessage);
}

// Test that the battery capacity routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunBatteryCapacityRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunBatteryCapacityRoutine(
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the battery health routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunBatteryHealthRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunBatteryHealthRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the urandom routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunUrandomRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunUrandomRoutine(
      /*length_seconds=*/120,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the smartctl check routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunSmartctlCheckRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunSmartctlCheckRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the AC power routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunAcPowerRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunAcPowerRoutine(
      /*expected_status=*/mojo_ipc::AcPowerStatusEnum::kConnected,
      /*expected_power_type=*/base::Optional<std::string>{"power_type"},
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the CPU cache routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunCpuCacheRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunCpuCacheRoutine(
      /*length_seconds=*/10,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the CPU stress routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunCpuStressRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunCpuStressRoutine(
      /*length_seconds=*/5 * 60,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the floating point accuracy routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunFloatingPointAccuracyRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunFloatingPointAccuracyRoutine(
      /*length_seconds=*/2 * 60,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the NVMe wear level routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunNvmeWearLevelRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunNvmeWearLevelRoutine(
      /*wear_level_threshold=*/30,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the nvme self-test routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunNvmeSelfTestRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunNvmeSelfTestRoutine(
      /*nvme_self_test_type=*/mojo_ipc::NvmeSelfTestTypeEnum::kShortSelfTest,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the disk read routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunDiskReadRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunDiskReadRoutine(
      /*type*/ mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead,
      /*length_seconds=*/10, /*file_size_mb=*/1024,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the prime search routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunPrimeSearchRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunPrimeSearchRoutine(
      /*length_seconds=*/10, /*max_num=*/1000000,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the battery discharge routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunBatteryDischargeRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  // TODO(crbug/1065463): Treat this as an interactive routine.
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunBatteryDischargeRoutine(
      /*length_seconds=*/23,
      /*maximum_discharge_percent_allowed=*/78,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the battery charge routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunBatteryChargeRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  // TODO(crbug/1065463): Treat this as an interactive routine.
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunBatteryChargeRoutine(
      /*length_seconds=*/54,
      /*minimum_charge_percent_required=*/56,
      base::BindLambdaForTesting(
          [&](mojo_ipc::RunRoutineResponsePtr received_response) {
            response = std::move(received_response);
            run_loop.Quit();
          }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the memory routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunMemoryRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunMemoryRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the LAN connectivity routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunLanConnectivityRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunLanConnectivityRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the signal strength routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunSignalStrengthRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunSignalStrengthRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the gateway can be pinged routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunGatewayCanBePingedRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunGatewayCanBePingedRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the has secure WiFi connection routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunHasSecureWiFiConnectionRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunHasSecureWiFiConnectionRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that the DNS resolver present routine can be run.
TEST_F(CrosHealthdRoutineServiceTest, RunDnsResolverPresentRoutine) {
  constexpr mojo_ipc::DiagnosticRoutineStatusEnum kExpectedStatus =
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  routine_factory()->SetNonInteractiveStatus(
      kExpectedStatus, /*status_message=*/"", /*progress_percent=*/50,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunDnsResolverPresentRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, 1);
  EXPECT_EQ(response->status, kExpectedStatus);
}

// Test that after a routine has been removed, we cannot access its data.
TEST_F(CrosHealthdRoutineServiceTest, AccessStoppedRoutine) {
  routine_factory()->SetNonInteractiveStatus(
      mojo_ipc::DiagnosticRoutineStatusEnum::kRunning, /*status_message=*/"",
      /*progress_percent=*/50, /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunSmartctlCheckRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  ExecuteGetRoutineUpdate(response->id,
                          mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
                          /*include_output=*/false);

  auto update = ExecuteGetRoutineUpdate(
      response->id, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      /*include_output=*/true);

  EXPECT_EQ(update->progress_percent, 0);
  VerifyNonInteractiveUpdate(update->routine_update_union,
                             mojo_ipc::DiagnosticRoutineStatusEnum::kError,
                             kRoutineDoesNotExistStatusMessage);
}

// Test that an unsupported routine cannot be run.
TEST_F(CrosHealthdRoutineServiceTest, RunUnsupportedRoutine) {
  mock_context()->fake_system_config()->SetSmartCtrlSupported(false);
  CreateService();
  routine_factory()->SetNonInteractiveStatus(
      mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported,
      /*status_message=*/"", /*progress_percent=*/0,
      /*output=*/"");

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunSmartctlCheckRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  EXPECT_EQ(response->id, mojo_ipc::kFailedToStartId);
  EXPECT_EQ(response->status,
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
    : public CrosHealthdRoutineServiceTest,
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

  mojo_ipc::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  service()->RunSmartctlCheckRoutine(base::BindLambdaForTesting(
      [&](mojo_ipc::RunRoutineResponsePtr received_response) {
        response = std::move(received_response);
        run_loop.Quit();
      }));
  run_loop.Run();

  auto update = ExecuteGetRoutineUpdate(response->id, params().command,
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
