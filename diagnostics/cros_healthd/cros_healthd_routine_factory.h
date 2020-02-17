// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>

#include "diagnostics/routines/diag_routine.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Interface for constructing DiagnosticRoutines.
class CrosHealthdRoutineFactory {
 public:
  virtual ~CrosHealthdRoutineFactory() = default;

  // Constructs a new instance of the urandom routine. See
  // diagnostics/routines/urandom for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeUrandomRoutine(
      uint32_t length_seconds) = 0;
  // Constructs a new instance of the battery capacity routine. See
  // diagnostics/routines/battery for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryCapacityRoutine(
      uint32_t low_mah, uint32_t high_mah) = 0;
  // Constructs a new instance of the battery health routine. See
  // diagnostics/routines/battery_sysfs for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeBatteryHealthRoutine(
      uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed) = 0;
  // Constructs a new instance of the smartctl check routine. See
  // diagnostics/routines/smartctl_check for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeSmartctlCheckRoutine() = 0;
  // Constructs a new instance of the AC power routine. See
  // diagnostics/routines/battery_sysfs for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) = 0;
  // Constructs a new instance of the CPU cache routine. See
  // diagnostics/routines/cpu_cache for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCpuCacheRoutine(
      const base::TimeDelta& exec_duration) = 0;
  // Constructs a new instance of the CPU stress routine. See
  // diagnostics/routines/cpu_stress for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeCpuStressRoutine(
      const base::TimeDelta& exec_duration) = 0;
  // Constructs a new instance of the floating point accuracy routine. See
  // diagnostics/routines/floating_point for details on the routine itself.
  virtual std::unique_ptr<DiagnosticRoutine> MakeFloatingPointAccuracyRoutine(
      const base::TimeDelta& exec_duration) = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_FACTORY_H_
