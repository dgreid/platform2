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
    CrosHealthdRoutineFactory* routine_factory)
    : routine_factory_(routine_factory) {
  DCHECK(routine_factory_);
}

CrosHealthdRoutineServiceImpl::~CrosHealthdRoutineServiceImpl() = default;

std::vector<mojo_ipc::DiagnosticRoutineEnum>
CrosHealthdRoutineServiceImpl::GetAvailableRoutines() {
  return available_routines_;
}

void CrosHealthdRoutineServiceImpl::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryCapacityRoutine(low_mah, high_mah),
             id, status);
}

void CrosHealthdRoutineServiceImpl::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeBatteryHealthRoutine(
                 maximum_cycle_count, percent_battery_wear_allowed),
             id, status);
}

void CrosHealthdRoutineServiceImpl::RunUrandomRoutine(
    uint32_t length_seconds,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeUrandomRoutine(length_seconds), id, status);
}

void CrosHealthdRoutineServiceImpl::RunSmartctlCheckRoutine(
    int32_t* id, mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeSmartctlCheckRoutine(), id, status);
}

void CrosHealthdRoutineServiceImpl::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeAcPowerRoutine(expected_status,
                                                  expected_power_type),
             id, status);
}

void CrosHealthdRoutineServiceImpl::RunCpuCacheRoutine(
    const base::TimeDelta& exec_duration,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeCpuCacheRoutine(exec_duration), id, status);
}

void CrosHealthdRoutineServiceImpl::RunCpuStressRoutine(
    const base::TimeDelta& exec_duration,
    int32_t* id,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  RunRoutine(routine_factory_->MakeCpuStressRoutine(exec_duration), id, status);
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
    int32_t* id_out,
    mojo_ipc::DiagnosticRoutineStatusEnum* status) {
  DCHECK(routine);
  DCHECK(id_out);
  DCHECK(status);

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

}  // namespace diagnostics
