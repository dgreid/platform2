// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>

#include "diagnostics/common/system/debugd_adapter.h"
#include "diagnostics/cros_healthd/system/system_config.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

void SetErrorRoutineUpdate(const std::string& status_message,
                           mojo_ipc::RoutineUpdate* response) {
  mojo_ipc::NonInteractiveRoutineUpdate noninteractive_update;
  noninteractive_update.status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  noninteractive_update.status_message = status_message;
  response->routine_update_union->set_noninteractive_update(
      noninteractive_update.Clone());
  response->progress_percent = 0;
}

}  // namespace

CrosHealthdRoutineService::CrosHealthdRoutineService(
    Context* context, CrosHealthdRoutineFactory* routine_factory)
    : context_(context), routine_factory_(routine_factory) {
  DCHECK(context_);
  DCHECK(routine_factory_);
  PopulateAvailableRoutines();
}

CrosHealthdRoutineService::~CrosHealthdRoutineService() = default;

void CrosHealthdRoutineService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  std::move(callback).Run(std::vector<mojo_ipc::DiagnosticRoutineEnum>(
      available_routines_.begin(), available_routines_.end()));
}

void CrosHealthdRoutineService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  mojo_ipc::RoutineUpdate update{0, mojo::ScopedHandle(),
                                 mojo_ipc::RoutineUpdateUnion::New()};

  auto itr = active_routines_.find(id);
  if (itr == active_routines_.end()) {
    LOG(ERROR) << "Bad id in GetRoutineUpdateRequest: " << id;
    SetErrorRoutineUpdate("Specified routine does not exist.", &update);
    std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
        update.progress_percent, std::move(update.output),
        std::move(update.routine_update_union)));
    return;
  }

  auto* routine = itr->second.get();
  switch (command) {
    case mojo_ipc::DiagnosticRoutineCommandEnum::kContinue:
      routine->Resume();
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kCancel:
      routine->Cancel();
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kGetStatus:
      // Retrieving the status and output of a routine is handled below.
      break;
    case mojo_ipc::DiagnosticRoutineCommandEnum::kRemove:
      routine->PopulateStatusUpdate(&update, include_output);
      if (update.routine_update_union->is_noninteractive_update()) {
        update.routine_update_union->get_noninteractive_update()->status =
            mojo_ipc::DiagnosticRoutineStatusEnum::kRemoved;
      }
      active_routines_.erase(itr);
      // |routine| is invalid at this point!
      std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
          update.progress_percent, std::move(update.output),
          std::move(update.routine_update_union)));
      return;
  }

  routine->PopulateStatusUpdate(&update, include_output);
  std::move(callback).Run(mojo_ipc::RoutineUpdate::New(
      update.progress_percent, std::move(update.output),
      std::move(update.routine_update_union)));
}

void CrosHealthdRoutineService::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeAcPowerRoutine(expected_status,
                                                  expected_power_type),
             mojo_ipc::DiagnosticRoutineEnum::kAcPower, std::move(callback));
}

void CrosHealthdRoutineService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryCapacityRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity,
             std::move(callback));
}

void CrosHealthdRoutineService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryChargeRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryChargeRoutine(
                 base::TimeDelta::FromSeconds(length_seconds),
                 minimum_charge_percent_required),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge,
             std::move(callback));
}

void CrosHealthdRoutineService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryDischargeRoutine(
                 base::TimeDelta::FromSeconds(length_seconds),
                 maximum_discharge_percent_allowed),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge,
             std::move(callback));
}

void CrosHealthdRoutineService::RunBatteryHealthRoutine(
    RunBatteryHealthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeBatteryHealthRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth,
             std::move(callback));
}

void CrosHealthdRoutineService::RunCaptivePortalRoutine(
    RunCaptivePortalRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeCaptivePortalRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal,
             std::move(callback));
}

void CrosHealthdRoutineService::RunCpuCacheRoutine(
    uint32_t length_seconds, RunCpuCacheRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeCpuCacheRoutine(
                 base::TimeDelta::FromSeconds(length_seconds)),
             mojo_ipc::DiagnosticRoutineEnum::kCpuCache, std::move(callback));
}

void CrosHealthdRoutineService::RunCpuStressRoutine(
    uint32_t length_seconds, RunCpuStressRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeCpuStressRoutine(
                 base::TimeDelta::FromSeconds(length_seconds)),
             mojo_ipc::DiagnosticRoutineEnum::kCpuStress, std::move(callback));
}

void CrosHealthdRoutineService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  RunRoutine(
      routine_factory_->MakeDiskReadRoutine(
          type, base::TimeDelta::FromSeconds(length_seconds), file_size_mb),
      mojo_ipc::DiagnosticRoutineEnum::kDiskRead, std::move(callback));
}

void CrosHealthdRoutineService::RunDnsLatencyRoutine(
    RunDnsLatencyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsLatencyRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsLatency, std::move(callback));
}

void CrosHealthdRoutineService::RunDnsResolutionRoutine(
    RunDnsResolutionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolutionRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsResolution,
             std::move(callback));
}

void CrosHealthdRoutineService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeDnsResolverPresentRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent,
             std::move(callback));
}

void CrosHealthdRoutineService::RunFloatingPointAccuracyRoutine(
    uint32_t length_seconds, RunFloatingPointAccuracyRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeFloatingPointAccuracyRoutine(
                 base::TimeDelta::FromSeconds(length_seconds)),
             mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
             std::move(callback));
}

void CrosHealthdRoutineService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeGatewayCanBePingedRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged,
             std::move(callback));
}

void CrosHealthdRoutineService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeHasSecureWiFiConnectionRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
             std::move(callback));
}

void CrosHealthdRoutineService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeLanConnectivityRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity,
             std::move(callback));
}

void CrosHealthdRoutineService::RunMemoryRoutine(
    RunMemoryRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeMemoryRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kMemory, std::move(callback));
}

void CrosHealthdRoutineService::RunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeSelfTestRoutine(
                 context_->debugd_adapter(), nvme_self_test_type),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest,
             std::move(callback));
}

void CrosHealthdRoutineService::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeNvmeWearLevelRoutine(
                 context_->debugd_adapter(), wear_level_threshold),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel,
             std::move(callback));
}

void CrosHealthdRoutineService::RunPrimeSearchRoutine(
    uint32_t length_seconds,
    uint64_t max_num,
    RunPrimeSearchRoutineCallback callback) {
  RunRoutine(routine_factory_->MakePrimeSearchRoutine(
                 base::TimeDelta::FromSeconds(length_seconds), max_num),
             mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
             std::move(callback));
}

void CrosHealthdRoutineService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSignalStrengthRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
             std::move(callback));
}

void CrosHealthdRoutineService::RunSmartctlCheckRoutine(
    RunSmartctlCheckRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeSmartctlCheckRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck,
             std::move(callback));
}

void CrosHealthdRoutineService::RunUrandomRoutine(
    uint32_t length_seconds, RunUrandomRoutineCallback callback) {
  RunRoutine(routine_factory_->MakeUrandomRoutine(length_seconds),
             mojo_ipc::DiagnosticRoutineEnum::kUrandom, std::move(callback));
}

void CrosHealthdRoutineService::RunRoutine(
    std::unique_ptr<DiagnosticRoutine> routine,
    mojo_ipc::DiagnosticRoutineEnum routine_enum,
    base::OnceCallback<void(mojo_ipc::RunRoutineResponsePtr)> callback) {
  DCHECK(routine);

  if (!available_routines_.count(routine_enum)) {
    LOG(ERROR) << routine_enum << " is not supported on this device";
    std::move(callback).Run(mojo_ipc::RunRoutineResponse::New(
        mojo_ipc::kFailedToStartId,
        mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported));
    return;
  }

  CHECK(next_id_ < std::numeric_limits<int32_t>::max())
      << "Maximum number of routines exceeded.";

  routine->Start();
  int32_t id = next_id_;
  DCHECK(active_routines_.find(id) == active_routines_.end());
  active_routines_[id] = std::move(routine);
  ++next_id_;

  std::move(callback).Run(
      mojo_ipc::RunRoutineResponse::New(id, active_routines_[id]->GetStatus()));
}

void CrosHealthdRoutineService::PopulateAvailableRoutines() {
  // Routines that are supported on all devices.
  available_routines_ = {
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kAcPower,
      mojo_ipc::DiagnosticRoutineEnum::kCpuCache,
      mojo_ipc::DiagnosticRoutineEnum::kCpuStress,
      mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
      mojo_ipc::DiagnosticRoutineEnum::kMemory,
      mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity,
      mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
      mojo_ipc::DiagnosticRoutineEnum::kGatewayCanBePinged,
      mojo_ipc::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
      mojo_ipc::DiagnosticRoutineEnum::kDnsResolverPresent,
      mojo_ipc::DiagnosticRoutineEnum::kDnsLatency,
      mojo_ipc::DiagnosticRoutineEnum::kDnsResolution,
      mojo_ipc::DiagnosticRoutineEnum::kCaptivePortal};

  if (context_->system_config()->HasBattery()) {
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity);
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth);
    available_routines_.insert(
        mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge);
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge);
  }

  if (context_->system_config()->NvmeSupported()) {
    if (context_->system_config()->IsWilcoDevice()) {
      available_routines_.insert(
          mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel);
    }
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest);
  }

  if (context_->system_config()->SmartCtlSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck);
  }

  if (context_->system_config()->FioSupported()) {
    available_routines_.insert(mojo_ipc::DiagnosticRoutineEnum::kDiskRead);
  }
}

}  // namespace diagnostics
