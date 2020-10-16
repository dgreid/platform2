// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fake/fake_routine_service.h"

#include <base/logging.h>

#include "mojo/nullable_primitives.mojom.h"

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

void FakeRoutineService::RunUrandomRoutine(
    mojo_ipc::NullableUint32Ptr length_seconds,
    RunUrandomRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryCapacityRoutine(
    RunBatteryCapacityRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunBatteryHealthRoutine(
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
    chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
    RunCpuCacheRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCpuStressRoutine(
    chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
    RunCpuStressRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunFloatingPointAccuracyRoutine(
    chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
    RunFloatingPointAccuracyRoutineCallback callback) {
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
    chromeos::cros_healthd::mojom::NullableUint32Ptr length_seconds,
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

void FakeRoutineService::RunLanConnectivityRoutine(
    RunLanConnectivityRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunSignalStrengthRoutine(
    RunSignalStrengthRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunGatewayCanBePingedRoutine(
    RunGatewayCanBePingedRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHasSecureWiFiConnectionRoutine(
    RunHasSecureWiFiConnectionRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsResolverPresentRoutine(
    RunDnsResolverPresentRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsLatencyRoutine(
    RunDnsLatencyRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunDnsResolutionRoutine(
    RunDnsResolutionRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunCaptivePortalRoutine(
    RunCaptivePortalRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHttpFirewallRoutine(
    RunHttpFirewallRoutineCallback callback) {
  NOTIMPLEMENTED();
}

void FakeRoutineService::RunHttpsFirewallRoutine(
    RunHttpsFirewallRoutineCallback callback) {
  NOTIMPLEMENTED();
}

}  // namespace diagnostics
