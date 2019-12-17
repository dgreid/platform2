// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/no_destructor.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
// Poll interval while waiting for a routine to finish.
constexpr base::TimeDelta kRoutinePollIntervalTimeDelta =
    base::TimeDelta::FromMilliseconds(100);
// Maximum time we're willing to wait for a routine to finish.
constexpr base::TimeDelta kMaximumRoutineExecutionTimeDelta =
    base::TimeDelta::FromSeconds(600);

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
    {"cpu_stress", mojo_ipc::DiagnosticRoutineEnum::kCpuStress}};

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

bool RunRoutineAndProcessResult(int32_t id,
                                diagnostics::CrosHealthdMojoAdapter* adapter) {
  auto response = adapter->GetRoutineUpdate(
      id, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
      true /* include_output */);

  const base::TimeTicks start_time = base::TimeTicks::Now();
  while (!response.is_null() &&
         response->routine_update_union->is_noninteractive_update() &&
         response->routine_update_union->get_noninteractive_update()->status ==
             mojo_ipc::DiagnosticRoutineStatusEnum::kRunning &&
         base::TimeTicks::Now() <
             start_time + kMaximumRoutineExecutionTimeDelta) {
    base::PlatformThread::Sleep(kRoutinePollIntervalTimeDelta);
    std::cout << "Progress: " << response->progress_percent << std::endl;

    response = adapter->GetRoutineUpdate(
        id, mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus,
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

    response = adapter->GetRoutineUpdate(
        id, mojo_ipc::DiagnosticRoutineCommandEnum::kContinue,
        false /* include_output */);
    return RunRoutineAndProcessResult(id, adapter);
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
    response = adapter->GetRoutineUpdate(
        id, mojo_ipc::DiagnosticRoutineCommandEnum::kRemove,
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

bool ActionGetRoutines() {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto reply = adapter.GetAvailableRoutines();
  for (auto routine : reply) {
    std::cout << "Available routine: " << GetSwitchFromRoutine(routine)
              << std::endl;
  }

  return true;
}

bool ActionRunBatteryCapacityRoutine(int low_mah, int high_mah) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunBatteryCapacityRoutine(low_mah, high_mah);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunBatteryHealthRoutine(int maximum_cycle_count,
                                   int percent_battery_wear_allowed) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunBatteryHealthRoutine(maximum_cycle_count,
                                                  percent_battery_wear_allowed);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunUrandomRoutine(int length_seconds) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunUrandomRoutine(length_seconds);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunSmartctlCheckRoutine() {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunSmartctlCheckRoutine();
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunAcPowerRoutine(bool is_connected, const std::string& power_type) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status =
      is_connected
          ? chromeos::cros_healthd::mojom::AcPowerStatusEnum::kConnected
          : chromeos::cros_healthd::mojom::AcPowerStatusEnum::kDisconnected;
  const base::Optional<std::string> optional_power_type =
      (power_type == "") ? base::nullopt
                         : base::Optional<std::string>{power_type};
  auto response =
      adapter.RunAcPowerRoutine(expected_status, optional_power_type);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunCpuCacheRoutine(const base::TimeDelta& exec_duration) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunCpuCacheRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

bool ActionRunCpuStressRoutine(const base::TimeDelta& exec_duration) {
  diagnostics::CrosHealthdMojoAdapter adapter;
  auto response = adapter.RunCpuStressRoutine(exec_duration);
  CHECK(response) << "No RunRoutineResponse received.";
  return RunRoutineAndProcessResult(response->id, &adapter);
}

}  // namespace

// 'diag' command-line tool:
//
// Test driver for libdiag. Only supports running a single diagnostic routine
// at a time.
int main(int argc, char** argv) {
  DEFINE_string(action, "",
                "Action to perform. Options are:\n\tget_routines - retrieve "
                "available routines.\n\trun_routine - run specified routine.");
  DEFINE_string(routine, "",
                "Diagnostic routine to run. For a list of available routines, "
                "run 'diag --action=get_routines'.");
  DEFINE_int32(low_mah, 1000, "Lower bound for the battery routine, in mAh.");
  DEFINE_int32(high_mah, 10000, "Upper bound for the battery routine, in mAh.");
  DEFINE_int32(
      maximum_cycle_count, 0,
      "Maximum cycle count allowed for the battery_sysfs routine to pass.");
  DEFINE_int32(percent_battery_wear_allowed, 100,
               "Maximum percent battery wear allowed for the battery_sysfs "
               "routine to pass.");
  DEFINE_int32(length_seconds, 10, "Number of seconds to run the routine for.");
  DEFINE_bool(ac_power_is_connected, true,
              "Whether or not the AC power routine expects the power supply to "
              "be connected.");
  DEFINE_string(
      expected_power_type, "",
      "Optional type of power supply expected for the AC power routine.");
  brillo::FlagHelper::Init(argc, argv, "diag - Device diagnostic tool.");

  logging::InitLogging(logging::LoggingSettings());

  base::AtExitManager at_exit_manager;

  base::MessageLoopForIO message_loop;

  if (FLAGS_action == "") {
    std::cout << "--action must be specified. Use --help for help on usage."
              << std::endl;
    return EXIT_FAILURE;
  }

  if (FLAGS_action == "get_routines")
    return ActionGetRoutines() ? EXIT_SUCCESS : EXIT_FAILURE;

  if (FLAGS_action == "run_routine") {
    std::map<std::string, mojo_ipc::DiagnosticRoutineEnum>
        switch_to_diagnostic_routine;
    for (const auto& item : kDiagnosticRoutineSwitches)
      switch_to_diagnostic_routine[item.switch_name] = item.routine;
    auto itr = switch_to_diagnostic_routine.find(FLAGS_routine);
    if (itr == switch_to_diagnostic_routine.end()) {
      std::cout << "Unknown routine: " << FLAGS_routine << std::endl;
      return EXIT_FAILURE;
    }

    bool routine_result;
    switch (itr->second) {
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity:
        routine_result =
            ActionRunBatteryCapacityRoutine(FLAGS_low_mah, FLAGS_high_mah);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth:
        routine_result = ActionRunBatteryHealthRoutine(
            FLAGS_maximum_cycle_count, FLAGS_percent_battery_wear_allowed);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kUrandom:
        routine_result = ActionRunUrandomRoutine(FLAGS_length_seconds);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck:
        routine_result = ActionRunSmartctlCheckRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAcPower:
        routine_result = ActionRunAcPowerRoutine(FLAGS_ac_power_is_connected,
                                                 FLAGS_expected_power_type);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuCache:
        routine_result = ActionRunCpuCacheRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuStress:
        routine_result = ActionRunCpuStressRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      default:
        std::cout << "Unsupported routine: " << FLAGS_routine << std::endl;
        return EXIT_FAILURE;
    }

    return routine_result ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  std::cout << "Unknown action: " << FLAGS_action << std::endl;
  return EXIT_FAILURE;
}
