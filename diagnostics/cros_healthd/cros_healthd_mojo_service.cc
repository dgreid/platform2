// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_mojo_service.h"

#include <sys/types.h>

#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/time/time.h>
#include <dbus/cros_healthd/dbus-constants.h>
#include <mojo/public/cpp/bindings/interface_request.h>

#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

CrosHealthdMojoService::CrosHealthdMojoService(
    FetchAggregator* fetch_aggregator,
    BluetoothEvents* bluetooth_events,
    LidEvents* lid_events,
    PowerEvents* power_events,
    CrosHealthdRoutineService* routine_service)
    : fetch_aggregator_(fetch_aggregator),
      bluetooth_events_(bluetooth_events),
      lid_events_(lid_events),
      power_events_(power_events),
      routine_service_(routine_service) {
  DCHECK(fetch_aggregator_);
  DCHECK(bluetooth_events_);
  DCHECK(lid_events_);
  DCHECK(power_events_);
  DCHECK(routine_service_);
}

CrosHealthdMojoService::~CrosHealthdMojoService() = default;

void CrosHealthdMojoService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  std::move(callback).Run(routine_service_->GetAvailableRoutines());
}

void CrosHealthdMojoService::GetRoutineUpdate(
    int32_t id,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  chromeos::cros_healthd::mojom::RoutineUpdate response{
      0, mojo::ScopedHandle(),
      chromeos::cros_healthd::mojom::RoutineUpdateUnion::New()};
  routine_service_->GetRoutineUpdate(id, command, include_output, &response);
  std::move(callback).Run(chromeos::cros_healthd::mojom::RoutineUpdate::New(
      response.progress_percent, std::move(response.output),
      std::move(response.routine_update_union)));
}

void CrosHealthdMojoService::RunUrandomRoutine(
    uint32_t length_seconds, RunUrandomRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunUrandomRoutine(length_seconds, &response.id,
                                      &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    RunBatteryCapacityRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryCapacityRoutine(low_mah, high_mah, &response.id,
                                              &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    RunBatteryHealthRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryHealthRoutine(maximum_cycle_count,
                                            percent_battery_wear_allowed,
                                            &response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunSmartctlCheckRoutine(
    RunSmartctlCheckRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunSmartctlCheckRoutine(&response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunAcPowerRoutine(expected_status, expected_power_type,
                                      &response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunCpuCacheRoutine(
    uint32_t length_seconds, RunCpuCacheRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunCpuCacheRoutine(
      base::TimeDelta().FromSeconds(length_seconds), &response.id,
      &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunCpuStressRoutine(
    uint32_t length_seconds, RunCpuStressRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunCpuStressRoutine(
      base::TimeDelta().FromSeconds(length_seconds), &response.id,
      &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunFloatingPointAccuracyRoutine(
    uint32_t length_seconds, RunFloatingPointAccuracyRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunFloatingPointAccuracyRoutine(
      base::TimeDelta::FromSeconds(length_seconds), &response.id,
      &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunNvmeWearLevelRoutine(wear_level_threshold, &response.id,
                                            &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunNvmeSelfTestRoutine(
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunNvmeSelfTestRoutine(nvme_self_test_type, &response.id,
                                           &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(length_seconds);
  routine_service_->RunDiskReadRoutine(type, exec_duration, file_size_mb,
                                       &response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunPrimeSearchRoutine(
    uint32_t length_seconds,
    uint64_t max_num,
    RunPrimeSearchRoutineCallback callback) {
  RunRoutineResponse response;
  base::TimeDelta exec_duration = base::TimeDelta::FromSeconds(length_seconds);

  routine_service_->RunPrimeSearchRoutine(exec_duration, max_num, &response.id,
                                          &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryDischargeRoutine(
      base::TimeDelta::FromSeconds(length_seconds),
      maximum_discharge_percent_allowed, &response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryChargeRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunBatteryChargeRoutine(
      base::TimeDelta::FromSeconds(length_seconds),
      minimum_charge_percent_required, &response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::RunMemoryRoutine(
    RunMemoryRoutineCallback callback) {
  RunRoutineResponse response;
  routine_service_->RunMemoryRoutine(&response.id, &response.status);
  std::move(callback).Run(response.Clone());
}

void CrosHealthdMojoService::AddBluetoothObserver(
    chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer) {
  bluetooth_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddLidObserver(
    chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer) {
  lid_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::AddPowerObserver(
    chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer) {
  power_events_->AddObserver(std::move(observer));
}

void CrosHealthdMojoService::ProbeProcessInfo(
    uint32_t process_id, ProbeProcessInfoCallback callback) {
  std::move(callback).Run(
      ProcessFetcher(static_cast<pid_t>(process_id)).FetchProcessInfo());
}

void CrosHealthdMojoService::ProbeTelemetryInfo(
    const std::vector<ProbeCategoryEnum>& categories,
    ProbeTelemetryInfoCallback callback) {
  return fetch_aggregator_->Run(categories, std::move(callback));
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

void CrosHealthdMojoService::AddEventBinding(
    chromeos::cros_healthd::mojom::CrosHealthdEventServiceRequest request) {
  event_binding_set_.AddBinding(this /* impl */, std::move(request));
}

}  // namespace diagnostics
