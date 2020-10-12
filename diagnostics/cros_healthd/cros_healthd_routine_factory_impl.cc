// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"

#include <base/logging.h>

#include "diagnostics/cros_healthd/routines/ac_power/ac_power.h"
#include "diagnostics/cros_healthd/routines/battery_capacity/battery_capacity.h"
#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"
#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"
#include "diagnostics/cros_healthd/routines/cpu_cache/cpu_cache.h"
#include "diagnostics/cros_healthd/routines/cpu_stress/cpu_stress.h"
#include "diagnostics/cros_healthd/routines/disk_read/disk_read.h"
#include "diagnostics/cros_healthd/routines/floating_point/floating_point_accuracy.h"
#include "diagnostics/cros_healthd/routines/gateway_can_be_pinged/gateway_can_be_pinged.h"
#include "diagnostics/cros_healthd/routines/lan_connectivity/lan_connectivity.h"
#include "diagnostics/cros_healthd/routines/memory/memory.h"
#include "diagnostics/cros_healthd/routines/nvme_self_test/nvme_self_test.h"
#include "diagnostics/cros_healthd/routines/nvme_wear_level/nvme_wear_level.h"
#include "diagnostics/cros_healthd/routines/prime_search/prime_search.h"
#include "diagnostics/cros_healthd/routines/signal_strength/signal_strength.h"
#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check.h"
#include "diagnostics/cros_healthd/routines/urandom/urandom.h"

namespace diagnostics {

CrosHealthdRoutineFactoryImpl::CrosHealthdRoutineFactoryImpl(Context* context)
    : context_(context) {
  DCHECK(context_);
}

CrosHealthdRoutineFactoryImpl::~CrosHealthdRoutineFactoryImpl() = default;

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeUrandomRoutine(uint32_t length_seconds) {
  return CreateUrandomRoutine(length_seconds);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryCapacityRoutine(uint32_t low_mah,
                                                          uint32_t high_mah) {
  return CreateBatteryCapacityRoutine(context_, low_mah, high_mah);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  return CreateBatteryHealthRoutine(context_, maximum_cycle_count,
                                    percent_battery_wear_allowed);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeSmartctlCheckRoutine() {
  return CreateSmartctlCheckRoutine();
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeAcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type) {
  return std::make_unique<AcPowerRoutine>(expected_status, expected_power_type);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCpuCacheRoutine(
    base::TimeDelta exec_duration) {
  return CreateCpuCacheRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCpuStressRoutine(
    base::TimeDelta exec_duration) {
  return CreateCpuStressRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeFloatingPointAccuracyRoutine(
    base::TimeDelta exec_duration) {
  return CreateFloatingPointAccuracyRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeNvmeWearLevelRoutine(
    DebugdAdapter* debugd_adapter, uint32_t wear_level_threshold) {
  DCHECK(debugd_adapter);
  return std::make_unique<NvmeWearLevelRoutine>(debugd_adapter,
                                                wear_level_threshold);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeNvmeSelfTestRoutine(
    DebugdAdapter* debugd_adapter,
    chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type) {
  DCHECK(debugd_adapter);

  NvmeSelfTestRoutine::SelfTestType type =
      nvme_self_test_type == chromeos::cros_healthd::mojom::
                                 NvmeSelfTestTypeEnum::kShortSelfTest
          ? NvmeSelfTestRoutine::kRunShortSelfTest
          : NvmeSelfTestRoutine::kRunLongSelfTest;

  return std::make_unique<NvmeSelfTestRoutine>(debugd_adapter, type);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeDiskReadRoutine(
    chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
    base::TimeDelta exec_duration,
    uint32_t file_size_mb) {
  return CreateDiskReadRoutine(type, exec_duration, file_size_mb);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakePrimeSearchRoutine(
    base::TimeDelta exec_duration, uint64_t max_num) {
  return CreatePrimeSearchRoutine(exec_duration, max_num);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryDischargeRoutine(
    base::TimeDelta exec_duration, uint32_t maximum_discharge_percent_allowed) {
  return std::make_unique<BatteryDischargeRoutine>(
      exec_duration, maximum_discharge_percent_allowed);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryChargeRoutine(
    base::TimeDelta exec_duration, uint32_t minimum_charge_percent_required) {
  return std::make_unique<BatteryChargeRoutine>(
      exec_duration, minimum_charge_percent_required);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeMemoryRoutine() {
  return std::make_unique<MemoryRoutine>(context_);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeLanConnectivityRoutine() {
  return CreateLanConnectivityRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeSignalStrengthRoutine() {
  return CreateSignalStrengthRoutine(context_->network_diagnostics_adapter());
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeGatewayCanBePingedRoutine() {
  return CreateGatewayCanBePingedRoutine(
      context_->network_diagnostics_adapter());
}

}  // namespace diagnostics
