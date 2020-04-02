// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/run_loop.h>
#include <mojo/public/cpp/bindings/interface_request.h>

namespace diagnostics {
namespace {
// Saves |response| to |response_destination|.
template <class T>
void OnMojoResponseReceived(T* response_destination,
                            base::Closure quit_closure,
                            T response) {
  *response_destination = std::move(response);
  quit_closure.Run();
}
}  // namespace

CrosHealthdMojoAdapter::CrosHealthdMojoAdapter(
    CrosHealthdMojoAdapterDelegate* delegate) {
  if (delegate) {
    delegate_ = delegate;
  } else {
    delegate_impl_ = std::make_unique<CrosHealthdMojoAdapterDelegateImpl>();
    delegate_ = delegate_impl_.get();
  }
  DCHECK(delegate_);
}

CrosHealthdMojoAdapter::~CrosHealthdMojoAdapter() = default;

chromeos::cros_healthd::mojom::TelemetryInfoPtr
CrosHealthdMojoAdapter::GetTelemetryInfo(
    const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
        categories_to_probe) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::TelemetryInfoPtr response;
  base::RunLoop run_loop;
  cros_healthd_probe_service_->ProbeTelemetryInfo(
      categories_to_probe,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::TelemetryInfoPtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunUrandomRoutine(uint32_t length_seconds) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunUrandomRoutine(
      length_seconds,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunBatteryCapacityRoutine(uint32_t low_mah,
                                                  uint32_t high_mah) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryCapacityRoutine(
      low_mah, high_mah,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryHealthRoutine(
      maximum_cycle_count, percent_battery_wear_allowed,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunSmartctlCheckRoutine() {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunSmartctlCheckRoutine(
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunAcPowerRoutine(
      expected_status, expected_power_type,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunCpuCacheRoutine(base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCpuCacheRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunCpuStressRoutine(base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunCpuStressRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunFloatingPointAccuracyRoutine(
      exec_duration.InSeconds(),
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunNvmeWearLevelRoutine(uint32_t wear_level_threshold) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeWearLevelRoutine(
      wear_level_threshold,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunNvmeSelfTestRoutine(
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunNvmeSelfTestRoutine(
      nvme_self_test_type,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunDiskReadRoutine(
    chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunDiskReadRoutine(
      type, exec_duration.InSeconds(), file_size_mb,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunPrimeSearchRoutine(base::TimeDelta exec_duration,
                                              uint64_t max_num) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunPrimeSearchRoutine(
      exec_duration.InSeconds(), max_num,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RunRoutineResponsePtr
CrosHealthdMojoAdapter::RunBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RunRoutineResponsePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->RunBatteryDischargeRoutine(
      exec_duration.InSeconds(), maximum_discharge_percent_allowed,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RunRoutineResponsePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
CrosHealthdMojoAdapter::GetAvailableRoutines() {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum> response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetAvailableRoutines(base::Bind(
      [](std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>* out,
         base::Closure quit_closure,
         const std::vector<
             chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>& routines) {
        *out = routines;
        quit_closure.Run();
      },
      &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

chromeos::cros_healthd::mojom::RoutineUpdatePtr
CrosHealthdMojoAdapter::GetRoutineUpdate(
    int32_t id,
    chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
    bool include_output) {
  if (!cros_healthd_service_factory_.is_bound())
    Connect();

  chromeos::cros_healthd::mojom::RoutineUpdatePtr response;
  base::RunLoop run_loop;
  cros_healthd_diagnostics_service_->GetRoutineUpdate(
      id, command, include_output,
      base::Bind(&OnMojoResponseReceived<
                     chromeos::cros_healthd::mojom::RoutineUpdatePtr>,
                 &response, run_loop.QuitClosure()));
  run_loop.Run();

  return response;
}

void CrosHealthdMojoAdapter::Connect() {
  cros_healthd_service_factory_ = delegate_->GetCrosHealthdServiceFactory();

  // Bind the probe and diagnostics services.
  cros_healthd_service_factory_->GetProbeService(
      mojo::MakeRequest(&cros_healthd_probe_service_));
  cros_healthd_service_factory_->GetDiagnosticsService(
      mojo::MakeRequest(&cros_healthd_diagnostics_service_));
}

}  // namespace diagnostics
