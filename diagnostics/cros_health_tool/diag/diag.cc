// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/diag.h"

#include <stdlib.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <string>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/task/single_thread_task_executor.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/cros_health_tool/diag/diag_actions.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace diagnostics {

namespace {

// Poll interval while waiting for a routine to finish.
constexpr base::TimeDelta kRoutinePollIntervalTimeDelta =
    base::TimeDelta::FromMilliseconds(100);
// Maximum time we're willing to wait for a routine to finish.
constexpr base::TimeDelta kMaximumRoutineExecutionTimeDelta =
    base::TimeDelta::FromHours(1);

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
    {"nvme_self_test", mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest},
    {"disk_read", mojo_ipc::DiagnosticRoutineEnum::kDiskRead},
    {"prime_search", mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch},
    {"battery_discharge", mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge},
    {"battery_charge", mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge},
    {"memory", mojo_ipc::DiagnosticRoutineEnum::kMemory},
    {"lan_connectivity", mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity},
    {"signal_strength", mojo_ipc::DiagnosticRoutineEnum::kSignalStrength},
    {"gateway_can_be_pinged",
     mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged},
    {"has_secure_wifi_connection",
     mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection},
    {"dns_resolver_present",
     mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent},
    {"dns_latency", mojo_ipc::DiagnosticRoutineEnum::kDnsLatency},
    {"dns_resolution", mojo_ipc::DiagnosticRoutineEnum::kDnsResolution},
    {"captive_portal", mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal},
    {"http_firewall", mojo_ipc::DiagnosticRoutineEnum::kHttpFirewall}};

}  // namespace

int diag_main(int argc, char** argv) {
  DEFINE_bool(crosh_help, false, "Display help specific to crosh usage.");
  DEFINE_string(action, "",
                "Action to perform. Options are:\n\tget_routines - retrieve "
                "available routines.\n\trun_routine - run specified routine.");
  DEFINE_string(routine, "",
                "Diagnostic routine to run. For a list of available routines, "
                "run 'diag --action=get_routines'.");
  DEFINE_uint32(force_cancel_at_percent, std::numeric_limits<uint32_t>::max(),
                "If specified, will attempt to cancel the routine when its "
                "progress exceeds the flag's value.\nValid range: [0, 100]");
  DEFINE_uint32(length_seconds, 10,
                "Number of seconds to run the routine for.");
  DEFINE_bool(ac_power_is_connected, true,
              "Whether or not the AC power routine expects the power supply to "
              "be connected.");
  DEFINE_string(
      expected_power_type, "",
      "Optional type of power supply expected for the AC power routine.");
  DEFINE_uint32(wear_level_threshold, 50,
                "Threshold number in percentage which routine examines "
                "wear level of NVMe against.");
  DEFINE_bool(nvme_self_test_long, false,
              "Long-time period self-test of NVMe would be performed with "
              "this flag being set.");
  DEFINE_int32(file_size_mb, 1024,
               "Size (MB) of the test file for disk_read routine to pass.");
  DEFINE_string(disk_read_routine_type, "linear",
                "Disk read routine type for the disk_read routine. Options are:"
                "\n\tlinear - linear read.\n\trandom - random read.");
  DEFINE_uint64(max_num, 1000000,
                "max. prime number to search for in "
                "prime-search routine. Max. is 1000000");
  DEFINE_uint32(maximum_discharge_percent_allowed, 100,
                "Upper bound for the battery discharge routine.");
  DEFINE_uint32(minimum_charge_percent_required, 0,
                "Lower bound for the battery charge routine.");
  brillo::FlagHelper::Init(argc, argv, "diag - Device diagnostic tool.");

  logging::InitLogging(logging::LoggingSettings());

  base::AtExitManager at_exit_manager;

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  if (FLAGS_crosh_help) {
    std::cout << "Usage: [list|routine]" << std::endl;
    return EXIT_SUCCESS;
  }

  if (FLAGS_action == "") {
    std::cout << "--action must be specified. Use --help for help on usage."
              << std::endl;
    return EXIT_FAILURE;
  }

  DiagActions actions{kRoutinePollIntervalTimeDelta,
                      kMaximumRoutineExecutionTimeDelta};

  if (FLAGS_action == "get_routines")
    return actions.ActionGetRoutines() ? EXIT_SUCCESS : EXIT_FAILURE;

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

    if (FLAGS_force_cancel_at_percent != std::numeric_limits<uint32_t>::max())
      actions.ForceCancelAtPercent(FLAGS_force_cancel_at_percent);

    bool routine_result;
    switch (itr->second) {
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity:
        routine_result = actions.ActionRunBatteryCapacityRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth:
        routine_result = actions.ActionRunBatteryHealthRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kUrandom:
        routine_result = actions.ActionRunUrandomRoutine(FLAGS_length_seconds);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck:
        routine_result = actions.ActionRunSmartctlCheckRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kAcPower:
        routine_result = actions.ActionRunAcPowerRoutine(
            FLAGS_ac_power_is_connected
                ? mojo_ipc::AcPowerStatusEnum::kConnected
                : mojo_ipc::AcPowerStatusEnum::kDisconnected,
            (FLAGS_expected_power_type == "")
                ? base::nullopt
                : base::Optional<std::string>{FLAGS_expected_power_type});
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuCache:
        routine_result = actions.ActionRunCpuCacheRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCpuStress:
        routine_result = actions.ActionRunCpuStressRoutine(
            base::TimeDelta().FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy:
        routine_result = actions.ActionRunFloatingPointAccuracyRoutine(
            base::TimeDelta::FromSeconds(FLAGS_length_seconds));
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel:
        routine_result =
            actions.ActionRunNvmeWearLevelRoutine(FLAGS_wear_level_threshold);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest:
        routine_result = actions.ActionRunNvmeSelfTestRoutine(
            FLAGS_nvme_self_test_long
                ? mojo_ipc::NvmeSelfTestTypeEnum::kLongSelfTest
                : mojo_ipc::NvmeSelfTestTypeEnum::kShortSelfTest);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDiskRead:
        mojo_ipc::DiskReadRoutineTypeEnum type;
        if (FLAGS_disk_read_routine_type == "linear") {
          type = mojo_ipc::DiskReadRoutineTypeEnum::kLinearRead;
        } else if (FLAGS_disk_read_routine_type == "random") {
          type = mojo_ipc::DiskReadRoutineTypeEnum::kRandomRead;
        } else {
          std::cout << "Unknown disk_read_routine_type: "
                    << FLAGS_disk_read_routine_type << std::endl;
          return EXIT_FAILURE;
        }
        routine_result = actions.ActionRunDiskReadRoutine(
            type, base::TimeDelta::FromSeconds(FLAGS_length_seconds),
            FLAGS_file_size_mb);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch:
        routine_result = actions.ActionRunPrimeSearchRoutine(
            base::TimeDelta::FromSeconds(FLAGS_length_seconds), FLAGS_max_num);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge:
        routine_result = actions.ActionRunBatteryDischargeRoutine(
            base::TimeDelta::FromSeconds(FLAGS_length_seconds),
            FLAGS_maximum_discharge_percent_allowed);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge:
        routine_result = actions.ActionRunBatteryChargeRoutine(
            base::TimeDelta::FromSeconds(FLAGS_length_seconds),
            FLAGS_minimum_charge_percent_required);
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity:
        routine_result = actions.ActionRunLanConnectivityRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kSignalStrength:
        routine_result = actions.ActionRunSignalStrengthRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kMemory:
        routine_result = actions.ActionRunMemoryRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged:
        routine_result = actions.ActionRunGatewayCanBePingedRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection:
        routine_result = actions.ActionRunHasSecureWiFiConnectionRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent:
        routine_result = actions.ActionRunDnsResolverPresentRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsLatency:
        routine_result = actions.ActionRunDnsLatencyRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kDnsResolution:
        routine_result = actions.ActionRunDnsResolutionRoutine();
        break;
      case mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal:
        routine_result = actions.ActionRunCaptivePortalRoutine();
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

}  // namespace diagnostics
