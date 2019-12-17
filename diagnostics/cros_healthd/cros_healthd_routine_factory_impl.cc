// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"

#include "diagnostics/routines/ac_power/ac_power.h"
#include "diagnostics/routines/battery/battery.h"
#include "diagnostics/routines/battery_sysfs/battery_sysfs.h"
#include "diagnostics/routines/cpu_cache/cpu_cache.h"
#include "diagnostics/routines/cpu_stress/cpu_stress.h"
#include "diagnostics/routines/smartctl_check/smartctl_check.h"
#include "diagnostics/routines/urandom/urandom.h"

namespace diagnostics {

CrosHealthdRoutineFactoryImpl::CrosHealthdRoutineFactoryImpl() = default;
CrosHealthdRoutineFactoryImpl::~CrosHealthdRoutineFactoryImpl() = default;

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeUrandomRoutine(uint32_t length_seconds) {
  return CreateUrandomRoutine(length_seconds);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryCapacityRoutine(uint32_t low_mah,
                                                          uint32_t high_mah) {
  return std::make_unique<BatteryRoutine>(low_mah, high_mah);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeBatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) {
  return std::make_unique<BatterySysfsRoutine>(maximum_cycle_count,
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
    const base::TimeDelta& exec_duration) {
  return CreateCpuCacheRoutine(exec_duration);
}

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeCpuStressRoutine(
    const base::TimeDelta& exec_duration) {
  return CreateCpuStressRoutine(exec_duration);
}

}  // namespace diagnostics
