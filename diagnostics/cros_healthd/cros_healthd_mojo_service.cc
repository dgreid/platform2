// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/time/time.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/cros_healthd/utils/cpu_utils.h"
#include "diagnostics/cros_healthd/utils/disk_utils.h"
#include "diagnostics/cros_healthd/utils/memory_utils.h"
#include "diagnostics/cros_healthd/utils/timezone_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    BacklightFetcher* backlight_fetcher,
    BatteryFetcher* battery_fetcher,
    CachedVpdFetcher* cached_vpd_fetcher,
    FanFetcher* fan_fetcher,
    CrosHealthdRoutineService* routine_service)
    : backlight_fetcher_(backlight_fetcher),
      battery_fetcher_(battery_fetcher),
      cached_vpd_fetcher_(cached_vpd_fetcher),
      fan_fetcher_(fan_fetcher),
      routine_service_(routine_service) {
  DCHECK(backlight_fetcher_);
  DCHECK(battery_fetcher_);
  DCHECK(cached_vpd_fetcher_);
  DCHECK(fan_fetcher_);
  DCHECK(routine_service_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::GetAvailableRoutines(
    const GetAvailableRoutinesCallback& callback) {
  callback.Run(routine_service_->GetAvailableRoutines());
}

void CrosHealthdMojoService::GetRoutineUpdate(
    int32_t id,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output,
    const GetRoutineUpdateCallback& callback) {
  chromeos::cros_healthd::mojom::RoutineUpdate response{
      0, mojo::ScopedHandle(),
      chromeos::cros_healthd::mojom::RoutineUpdateUnion::New()};
  routine_service_->GetRoutineUpdate(id, command, include_output, &response);
  callback.Run(chromeos::cros_healthd::mojom::RoutineUpdate::New(
      response.progress_percent, std::move(response.output),
      std::move(response.routine_update_union)));
}

void CrosHealthdMojoService::RunUrandomRoutine(
    uint32_t length_seconds, const RunUrandomRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunUrandomRoutine(length_seconds, &response.id,
                                      &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    const RunBatteryCapacityRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryCapacityRoutine(low_mah, high_mah, &response.id,
                                              &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    const RunBatteryHealthRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryHealthRoutine(maximum_cycle_count,
                                            percent_battery_wear_allowed,
                                            &response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunSmartctlCheckRoutine(
    const RunSmartctlCheckRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunSmartctlCheckRoutine(&response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    const RunAcPowerRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunAcPowerRoutine(expected_status, expected_power_type,
                                      &response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunCpuCacheRoutine(
    uint32_t length_seconds, const RunCpuCacheRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunCpuCacheRoutine(
      base::TimeDelta().FromSeconds(length_seconds), &response.id,
      &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunCpuStressRoutine(
    uint32_t length_seconds, const RunCpuStressRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunCpuStressRoutine(
      base::TimeDelta().FromSeconds(length_seconds), &response.id,
      &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunFloatingPointAccuracyRoutine(
    uint32_t length_seconds,
    const RunFloatingPointAccuracyRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunFloatingPointAccuracyRoutine(
      base::TimeDelta::FromSeconds(length_seconds), &response.id,
      &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold,
    const RunNvmeWearLevelRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunNvmeWearLevelRoutine(wear_level_threshold, &response.id,
                                            &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunNvmeSelfTestRoutine(
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
    const RunNvmeSelfTestRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunNvmeSelfTestRoutine(nvme_self_test_type, &response.id,
                                           &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    const RunDiskReadRoutineCallback& callback) {
  RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(length_seconds);
  routine_service_->RunDiskReadRoutine(type, exec_duration, file_size_mb,
                                       &response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunPrimeSearchRoutine(
    uint32_t length_seconds,
    uint64_t max_num,
    const RunPrimeSearchRoutineCallback& callback) {
  RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(length_seconds);

  routine_service_->RunPrimeSearchRoutine(exec_duration, max_num, &response.id,
                                          &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    const RunBatteryDischargeRoutineCallback& callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryDischargeRoutine(
      base::TimeDelta::FromSeconds(length_seconds),
      maximum_discharge_percent_allowed, &response.id, &response.status);
  callback.Run(response.Clone());
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    const ProbeTelemetryInfoCallback& callback) {
  chromeos::cros_healthd::mojom::TelemetryInfo telemetry_info;
  for (const auto category : categories) {
    switch (category) {
      case ProbeCategoryEnum::kBattery: {
        telemetry_info.battery_info = battery_fetcher_->FetchBatteryInfo();
        break;
      }
      case ProbeCategoryEnum::kCachedVpdData: {
        telemetry_info.vpd_info =
            cached_vpd_fetcher_->FetchCachedVpdInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kCpu: {
        telemetry_info.cpu_result = FetchCpuInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kNonRemovableBlockDevices: {
        telemetry_info.block_device_info = base::Optional<std::vector<
            chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>>(
            FetchNonRemovableBlockDevicesInfo(base::FilePath("/")));
        break;
      }
      case ProbeCategoryEnum::kTimezone: {
        telemetry_info.timezone_result = FetchTimezoneInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kMemory: {
        telemetry_info.memory_info = FetchMemoryInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kBacklight: {
        telemetry_info.backlight_info =
            backlight_fetcher_->FetchBacklightInfo(base::FilePath("/"));
        break;
      }
      case ProbeCategoryEnum::kFan: {
        telemetry_info.fan_result =
            fan_fetcher_->FetchFanInfo(base::FilePath("/"));
        break;
      }
    }
  }

  callback.Run(telemetry_info.Clone());
}

void CrosHealthdMojoService::AddProbeBinding(
    chromeos::cros_healthd::mojom::CrosHealthdProbeServiceRequest request) {
  probe_binding_set_.AddBinding(this /* impl */, std::move(request));
}

void CrosHealthdMojoService::AddDiagnosticsBinding(
    chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServiceRequest
        request) {
  diagnostics_binding_set_.AddBinding(this /* impl */, std::move(request));
}

}  // namespace diagnostics
