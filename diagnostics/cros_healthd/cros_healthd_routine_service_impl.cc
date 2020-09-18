// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_service_impl.h"

#include <limits>
#include <string>
#include <utility>

#include <base/logging.h>

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

CrosHealthdRoutineServiceImpl::CrosHealthdRoutineServiceImpl(
    Context* context, CrosHealthdRoutineFactory* routine_factory)
    : context_(context), routine_factory_(routine_factory) {
  DCHECK(context_);
  DCHECK(routine_factory_);
  PopulateAvailableRoutines();
}

CrosHealthdRoutineServiceImpl::~CrosHealthdRoutineServiceImpl() = default;

std::vector<mojo_ipc::DiagnosticRoutineEnum>
CrosHealthdRoutineServiceImpl::GetAvailableRoutines() {
  return std::vector<mojo_ipc::DiagnosticRoutineEnum>(
      available_routines_.begin(), available_routines_.end());
}

void CrosHealthdRoutineServiceImpl::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryCapacityRoutine(low_mah, high_mah),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryCapacity, id, status);
}

void CrosHealthdRoutineServiceImpl::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryHealthRoutine(
                 maximum_cycle_count, percent_battery_wear_allowed),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryHealth, id, status);
}

void CrosHealthdRoutineServiceImpl::RunUrandomRoutine(
    uint32_t length_seconds,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeUrandomRoutine(length_seconds),
             mojo_ipc::DiagnosticRoutineEnum::kUrandom, id, status);
}

void CrosHealthdRoutineServiceImpl::RunSmartctlCheckRoutine(
    int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeSmartctlCheckRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kSmartctlCheck, id, status);
}

void CrosHealthdRoutineServiceImpl::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeAcPowerRoutine(expected_status,
                                                  expected_power_type),
             mojo_ipc::DiagnosticRoutineEnum::kAcPower, id, status);
}

void CrosHealthdRoutineServiceImpl::RunCpuCacheRoutine(
    base::TimeDelta exec_duration,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeCpuCacheRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kCpuCache, id, status);
}

void CrosHealthdRoutineServiceImpl::RunCpuStressRoutine(
    base::TimeDelta exec_duration,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeCpuStressRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kCpuStress, id, status);
}

void CrosHealthdRoutineServiceImpl::RunFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeFloatingPointAccuracyRoutine(exec_duration),
             mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy, id,
             status);
}

void CrosHealthdRoutineServiceImpl::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeNvmeWearLevelRoutine(
                 context_->debugd_adapter(), wear_level_threshold),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeWearLevel, id, status);
}

void CrosHealthdRoutineServiceImpl::RunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeNvmeSelfTestRoutine(
                 context_->debugd_adapter(), nvme_self_test_type),
             mojo_ipc::DiagnosticRoutineEnum::kNvmeSelfTest, id, status);
}

void CrosHealthdRoutineServiceImpl::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(
      routine_factory_->MakeDiskReadRoutine(type, exec_duration, file_size_mb),
      mojo_ipc::DiagnosticRoutineEnum::kDiskRead, id, status);
}

void CrosHealthdRoutineServiceImpl::RunPrimeSearchRoutine(
    base::TimeDelta exec_duration,
    uint64_t max_num,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakePrimeSearchRoutine(exec_duration, max_num),
             mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch, id, status);
}

void CrosHealthdRoutineServiceImpl::RunBatteryDischargeRoutine(
    base::TimeDelta exec_duration,
    uint32_t maximum_discharge_percent_allowed,
    int32_t* id,
    MojomCrosHealthdDiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryDischargeRoutine(
                 exec_duration, maximum_discharge_percent_allowed),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryDischarge, id, status);
}

void CrosHealthdRoutineServiceImpl::RunBatteryChargeRoutine(
    base::TimeDelta exec_duration,
    uint32_t minimum_charge_percent_required,
    int32_t* id,
    MojomCrosHealthdDiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryChargeRoutine(
                 exec_duration, minimum_charge_percent_required),
             mojo_ipc::DiagnosticRoutineEnum::kBatteryCharge, id, status);
}

void CrosHealthdRoutineServiceImpl::RunMemoryRoutine(
    int32_t* id, MojomCrosHealthdDiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeMemoryRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kMemory, id, status);
}

void CrosHealthdRoutineServiceImpl::RunLanConnectivityRoutine(
    int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeLanConnectivityRoutine(),
             mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity, id, status);
}

void CrosHealthdRoutineServiceImpl::GetRoutineUpdate(
    int32_t uuid,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    mojo_ipc::RoutineUpdate* response) {
  auto itr = active_routines_.find(uuid);
  if (itr == active_routines_.end()) {
    LOG(ERROR) << "Bad uuid in GetRoutineUpdateRequest.";
    SetErrorRoutineUpdate("Specified routine does not exist.", response);
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
      routine->PopulateStatusUpdate(response, include_output);
      if (response->routine_update_union->is_noninteractive_update()) {
        response->routine_update_union->get_noninteractive_update()->status =
            MojomCrosHealthdDiagnosticRoutineStatusEnum::kRemoved;
      }
      active_routines_.erase(itr);
      // |routine| is invalid at this point!
      return;
  }

  routine->PopulateStatusUpdate(response, include_output);
}

void CrosHealthdRoutineServiceImpl::RunRoutine(
    std::unique_ptr<DiagnosticRoutine> routine,
    mojo_ipc::DiagnosticRoutineEnum routine_enum,
    int32_t* id_out,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  DCHECK(routine);
  DCHECK(id_out);
  DCHECK(status);

  if (!available_routines_.count(routine_enum)) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kUnsupported;
    *id_out = mojo_ipc::kFailedToStartId;
    LOG(ERROR) << routine_enum << " is not supported on this device";
    return;
  }

  CHECK(next_id_ < std::numeric_limits<int32_t>::max())
      << "Maximum number of routines exceeded.";

  routine->Start();
  int32_t id = next_id_;
  DCHECK(active_routines_.find(id) == active_routines_.end());
  active_routines_[id] = std::move(routine);
  ++next_id_;

  *id_out = id;
  *status = active_routines_[id]->GetStatus();
}

void CrosHealthdRoutineServiceImpl::PopulateAvailableRoutines() {
  // Routines that are supported on all devices.
  available_routines_ = {
      mojo_ipc::DiagnosticRoutineEnum::kUrandom,
      mojo_ipc::DiagnosticRoutineEnum::kAcPower,
      mojo_ipc::DiagnosticRoutineEnum::kCpuCache,
      mojo_ipc::DiagnosticRoutineEnum::kCpuStress,
      mojo_ipc::DiagnosticRoutineEnum::kFloatingPointAccuracy,
      mojo_ipc::DiagnosticRoutineEnum::kPrimeSearch,
      mojo_ipc::DiagnosticRoutineEnum::kMemory,
      mojo_ipc::DiagnosticRoutineEnum::kLanConnectivity};

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
