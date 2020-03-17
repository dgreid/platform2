// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/diag/diag_actions.h"

#include <cstdint>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

const struct {
  const char* switch_name;
  mojo_ipc::DiagnosticRoutineEnum routine;
} kDiagnosticRoutineSwitches[] = {
    {"battery_capacity", mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity},
    {"battery_health", mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth},
    {"urandom", mojo_ipc::DiagnosticRoutineEnum::kUrandom},
    {"smartctl_check", mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck},
    {"ac_power", mojo_ipc::DiagnosticRoutineEnum::kAcPower},
    {"cpu_cache", mojo_ipc::DiagnosticRoutineEnum::kCpuCache},
    {"cpu_stress", mojo_ipc::DiagnosticRoutineEnum::kCpuStress},
    {"floating_point_accuracy",
     mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy},
    {"nvme_wear_level", mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel},
    {"nvme_self_test", mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest}};

const struct {
  const char* readable_status;
  mojo_ipc::DiagnosticRoutineStatusEnum status;
} kDiagnosticRoutineReadableStatuses[] = {
    {"Ready", mojo_ipc::DiagnosticRoutineStatusEnum::kReady},
    {"Running", mojo_ipc::DiagnosticRoutineStatusEnum::kRunning},
    {"Waiting", mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting},
    {"Passed", mojo_ipc::DiagnosticRoutineStatusEnum::kPassed},
    {"Failed", mojo_ipc::DiagnosticRoutineStatusEnum::kFailed},
    {"Error", mojo_ipc::DiagnosticRoutineStatusEnum::kError},
    {"Cancelled", mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled},
    {"Failed to start", mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart},
    {"Removed", mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved},
    {"Cancelling", mojo_ipc::DiagnosticRoutineStatusEnum::kCancelling}};

const struct {
  const char* readable_user_message;
  mojo_ipc::DiagnosticRoutineUserMessageEnum user_message_enum;
} kDiagnosticRoutineReadableUserMessages[] = {
    {"Unplug the AC adapter.",
     mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower},
    {"Plug in the AC adapter.",
     mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower}};

std::string GetSwitchFromRoutine(mojo_ipc::DiagnosticRoutineEnum routine) {
  static base::NoDestructor<
      std::map<mojo_ipc::DiagnosticRoutineEnum, std::string>>
      diagnostic_routine_to_switch;

  if (diagnostic_routine_to_switch->empty()) {
    for (const auto& item : kDiagnosticRoutineSwitches) {
      diagnostic_routine_to_switch->insert(
          std::make_pair(item.routine, item.switch_name));
    }
  }

  auto routine_itr = diagnostic_routine_to_switch->find(routine);
  LOG_IF(FATAL, routine_itr == diagnostic_routine_to_switch->end())
      << "Invalid routine to switch lookup with routine: " << routine;

  return routine_itr->second;
}

}  // namespace

DiagActions::DiagActions(base::TimeDelta polling_interval,
                         base::TimeDelta maximum_execution_time)
    : kPollingInterval(polling_interval),
      kMaximumExecutionTime(maximum_execution_time) {}

DiagActions::~DiagActions() = default;

bool DiagActions::ActionGetRoutines() {
  auto reply = adapter_.GetAvailableRoutines();
  for (auto routine : reply) {
    std::cout << "Available routine: " << GetSwitchFromRoutine(routine)
              << std::endl;
  }

  return true;
}

bool DiagActions::ActionRunAcPowerRoutine(bool is_connected,
                                          const std::string& power_type) {
  chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status =
      is_connected
          ? chromeos::cros_healthd::mojom::AcPowerStatusEnum::kConnected
          : chromeos::cros_healthd::mojom::AcPowerStatusEnum::kDisconnected;
  const base::Optional<std::string> optional_power_type =
      (power_type == "") ? base::nullopt
                         : base::Optional<std::string>{power_type};
  auto response =
      adapter_.RunAcPowerRoutine(expected_status, optional_power_type);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunBatteryCapacityRoutine(uint32_t low_mah,
                                                  uint32_t high_mah) {
  auto response = adapter_.RunBatteryCapacityRoutine(low_mah, high_mah);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  auto response = adapter_.RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunCpuCacheRoutine(
    const base::TimeDelta& exec_duration) {
  auto response = adapter_.RunCpuCacheRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunCpuStressRoutine(
    const base::TimeDelta& exec_duration) {
  auto response = adapter_.RunCpuStressRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunFloatingPointAccuracyRoutine(
    const base::TimeDelta& exec_duration) {
  auto response = adapter_.RunFloatingPointAccuracyRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunNvmeSelfTestRoutine(bool is_long) {
  chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum type =
      is_long
          ? chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum::kLongSelfTest
          : chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum::kShortSelfTest;
  auto response = adapter_.RunNvmeSelfTestRoutine(type);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunNvmeWearLevelRoutine(uint32_t wear_level_threshold) {
  auto response = adapter_.RunNvmeWearLevelRoutine(wear_level_threshold);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunSmartctlCheckRoutine() {
  auto response = adapter_.RunSmartctlCheckRoutine();
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::ActionRunUrandomRoutine(uint32_t length_seconds) {
  auto response = adapter_.RunUrandomRoutine(length_seconds);
  CHECK(response) << "No RunRoutineResponse received.";
  id_ = response->id;
  return RunRoutineAndProcessResult();
}

bool DiagActions::RunRoutineAndProcessResult() {
  auto response = adapter_.GetRoutineUpdate(
      id_, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      true /* include_output */);

  const base::TimeTicks start_time = base::TimeTicks::Now();
  while (!response.is_null() &&
         response->routine_update_union->is_noninteractive_update() &&
         response->routine_update_union->get_noninteractive_update()->status ==
             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning &&
         base::TimeTicks::Now() < start_time + kMaximumExecutionTime) {
    base::PlatformThread::Sleep(kPollingInterval);
    std::cout << "Progress: " << response->progress_percent << std::endl;

    response = adapter_.GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
        true /* include_output */);
  }

  if (response.is_null()) {
    std::cout << "No GetRoutineUpdateResponse received." << std::endl;
    return false;
  }

  // Interactive updates require us to print out instructions to the user on the
  // console. Once the user responds by pressing the ENTER key, we need to send
  // a continue command to the routine and restart waiting for results.
  if (response->routine_update_union->is_interactive_update()) {
    bool user_message_found = false;
    mojo_ipc::DiagnosticRoutineUserMessageEnum user_message =
        response->routine_update_union->get_interactive_update()->user_message;
    for (const auto& item : kDiagnosticRoutineReadableUserMessages) {
      if (item.user_message_enum == user_message) {
        user_message_found = true;
        std::cout << item.readable_user_message << std::endl
                  << "Press ENTER to continue." << std::endl;
        break;
      }
    }
    LOG_IF(FATAL, !user_message_found)
        << "No readable message found for DiagnosticRoutineUserMessageEnum: "
        << user_message;

    std::string dummy;
    std::getline(std::cin, dummy);

    response = adapter_.GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kContinue,
        false /* include_output */);
    return RunRoutineAndProcessResult();
  }

  // Noninteractive routines without a status of kRunning must have terminated
  // in some form. Print the update to the console to let the user know.
  if (response->output.is_valid()) {
    auto shared_memory = diagnostics::GetReadOnlySharedMemoryFromMojoHandle(
        std::move(response->output));
    if (shared_memory) {
      std::cout << "Output: "
                << std::string(
                       static_cast<const char*>(shared_memory->memory()),
                       shared_memory->mapped_size())
                << std::endl;
    } else {
      LOG(ERROR) << "Failed to read output.";
      return false;
    }
  }

  std::cout << "Progress: " << response->progress_percent << std::endl;
  bool status_found = false;
  mojo_ipc::DiagnosticRoutineStatusEnum status =
      response->routine_update_union->get_noninteractive_update()->status;
  for (const auto& item : kDiagnosticRoutineReadableStatuses) {
    if (item.status == status) {
      status_found = true;
      std::cout << "Status: " << item.readable_status << std::endl;
      break;
    }
  }
  LOG_IF(FATAL, !status_found)
      << "Invalid readable status lookup with status: " << status;

  std::cout << "Status message: "
            << response->routine_update_union->get_noninteractive_update()
                   ->status_message
            << std::endl;

  if (status != mojo_ipc::DiagnosticRoutineStatusEnum::kFailedToStart) {
    response = adapter_.GetRoutineUpdate(
        id_, mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
        false /* include_output */);

    if (response.is_null() ||
        !response->routine_update_union->is_noninteractive_update() ||
        response->routine_update_union->get_noninteractive_update()->status !=
            mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved) {
      std::cout << "Failed to remove routine." << std::endl;
      return false;
    }
  }

  return true;
}

}  // namespace diagnostics
