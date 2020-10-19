// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/fake_diagnostics_service.h"

#include <utility>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

FakeDiagnosticsService::FakeDiagnosticsService() = default;
FakeDiagnosticsService::~FakeDiagnosticsService() = default;

bool FakeDiagnosticsService::GetCrosHealthdDiagnosticsService(
    mojo_ipc::CrosHealthdDiagnosticsServiceRequest service) {
  // In situations where cros_healthd is unresponsive, the delegate wouldn't
  // know this, and would think that it had passed along the service request
  // and everything is fine. However, nothing would bind that request.
  if (!is_responsive_)
    return true;

  // In situations where wilco_dtc_supportd's mojo service hasn't been set up
  // yet, the delegate would realize this and report failure.
  if (!is_available_)
    return false;

  // When there are no errors with the request, it will be bound.
  service_binding_.Bind(std::move(service));
  return true;
}

void FakeDiagnosticsService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  std::move(callback).Run(available_routines_);
}

void FakeDiagnosticsService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
      routine_update_response_.progress_percent,
      std::move(routine_update_response_.output),
      std::move(routine_update_response_.routine_update_union)));
}

void FakeDiagnosticsService::RunUrandomRoutine(
    uint32_t length_seconds, RunUrandomRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryHealthRoutine(
    RunBatteryHealthRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunSmartctlCheckRoutine(
    RunSmartctlCheckRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunCpuCacheRoutine(
    uint32_t length_seconds, RunCpuCacheRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunCpuStressRoutine(
    uint32_t length_seconds, RunCpuStressRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunFloatingPointAccuracyRoutine(
    uint32_t length_seconds, RunFloatingPointAccuracyRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunNvmeSelfTestRoutine(
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunPrimeSearchRoutine(
    uint32_t length_seconds,
    uint64_t max_num,
    RunPrimeSearchRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryChargeRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunMemoryRoutine(
    RunMemoryRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  std::move(callback).Run(run_routine_response_.Clone());
}

void FakeDiagnosticsService::SetMojoServiceIsAvailable(bool is_available) {
  is_available_ = is_available;
}

void FakeDiagnosticsService::SetMojoServiceIsResponsive(bool is_responsive) {
  is_responsive_ = is_responsive;
}

void FakeDiagnosticsService::ResetMojoConnection() {
  service_binding_.Close();
}

void FakeDiagnosticsService::SetGetAvailableRoutinesResponse(
    const std::vector<mojo_ipc::DiagnosticRoutineEnum>& available_routines) {
  available_routines_ = available_routines;
}

void FakeDiagnosticsService::SetInteractiveUpdate(
    mojo_ipc::DiagnosticRoutineUserMessageEnum user_message,
    uint32_t progress_percent,
    const std::string& output) {
  routine_update_response_.progress_percent = progress_percent;
  routine_update_response_.output =
      CreateReadOnlySharedMemoryRegionMojoHandle(output);
  mojo_ipc::InteractiveRoutineUpdate interactive_update;
  interactive_update.user_message = user_message;
  routine_update_response_.routine_update_union->set_interactive_update(
      interactive_update.Clone());
}

void FakeDiagnosticsService::SetNonInteractiveUpdate(
    mojo_ipc::DiagnosticRoutineStatusEnum status,
    const std::string& status_message,
    uint32_t progress_percent,
    const std::string& output) {
  routine_update_response_.progress_percent = progress_percent;
  routine_update_response_.output =
      CreateReadOnlySharedMemoryRegionMojoHandle(output);
  mojo_ipc::NonInteractiveRoutineUpdate noninteractive_update;
  noninteractive_update.status = status;
  noninteractive_update.status_message = status_message;
  routine_update_response_.routine_update_union->set_noninteractive_update(
      noninteractive_update.Clone());
}

void FakeDiagnosticsService::SetRunSomeRoutineResponse(
    uint32_t id, mojo_ipc::DiagnosticRoutineStatusEnum status) {
  run_routine_response_.id = id;
  run_routine_response_.status = status;
}

}  // namespace diagnostics
