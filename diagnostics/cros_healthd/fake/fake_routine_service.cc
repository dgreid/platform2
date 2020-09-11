// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_routine_service.h"

#include <base/logging.h>

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

FakeRoutineService::FakeRoutineService() = default;
FakeRoutineService::~FakeRoutineService() = default;

void FakeRoutineService::GetAvailableRoutines(
    GetAvailableRoutinesCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::GetRoutineUpdate(
    int32_t id,
    mojo_ipc::DiagnosticRoutineCommandEnum command,
    bool include_output,
    GetRoutineUpdateCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunUrandomRoutine(uint32_t length_seconds,
                                           RunUrandomRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryCapacityRoutine(
    uint32_t low_mah,
    uint32_t high_mah,
    RunBatteryCapacityRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryHealthRoutine(
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed,
    RunBatteryHealthRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunSmartctlCheckRoutine(
    RunSmartctlCheckRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunAcPowerRoutine(
    mojo_ipc::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    RunAcPowerRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCpuCacheRoutine(
    uint32_t length_seconds, RunCpuCacheRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCpuStressRoutine(
    uint32_t length_seconds, RunCpuStressRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunFloatingPointAccuracyRoutine(
    uint32_t length_seconds, RunFloatingPointAccuracyRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunNvmeWearLevelRoutine(
    uint32_t wear_level_threshold, RunNvmeWearLevelRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunNvmeSelfTestRoutine(
    mojo_ipc::NvmeSelfTestTypeEnum nvme_self_test_type,
    RunNvmeSelfTestRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDiskReadRoutine(
    mojo_ipc::DiskReadRoutineTypeEnum type,
    uint32_t length_seconds,
    uint32_t file_size_mb,
    RunDiskReadRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunPrimeSearchRoutine(
    uint32_t length_seconds,
    uint64_t max_num,
    RunPrimeSearchRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryDischargeRoutine(
    uint32_t length_seconds,
    uint32_t maximum_discharge_percent_allowed,
    RunBatteryDischargeRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryChargeRoutine(
    uint32_t length_seconds,
    uint32_t minimum_charge_percent_required,
    RunBatteryDischargeRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunMemoryRoutine(RunMemoryRoutineCallback callback) {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
