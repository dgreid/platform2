// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/cros_healthd_routine_factory_impl.h"

#include "diagnostics/routines/ac_power/ac_power.h"
#include "diagnostics/routines/battery/battery.h"
#include "diagnostics/routines/battery_sysfs/battery_sysfs.h"
#include "diagnostics/routines/cpu_cache/cpu_cache.h"
#include "diagnostics/routines/cpu_stress/cpu_stress.h"
#include "diagnostics/routines/floating_point/floating_point_accuracy.h"
#include "diagnostics/routines/nvme_self_test/nvme_self_test.h"
#include "diagnostics/routines/nvme_wear_level/nvme_wear_level.h"
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

std::unique_ptr<DiagnosticRoutine>
CrosHealthdRoutineFactoryImpl::MakeFloatingPointAccuracyRoutine(
    const base::TimeDelta& exec_duration) {
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

}  // namespace diagnostics
