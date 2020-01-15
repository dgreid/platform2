// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>

#include "diagnostics/common/system/debugd_adapter_impl.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/routines/diag_routine.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Production implementation of the CrosHealthdRoutineService interface.
class CrosHealthdRoutineServiceImpl final : public CrosHealthdRoutineService {
 public:
  CrosHealthdRoutineServiceImpl(DebugdAdapter* debugd_adapter,
                                CrosHealthdRoutineFactory* routine_factory);
  ~CrosHealthdRoutineServiceImpl() override;

  // CrosHealthdRoutineService overrides:
  std::vector<MojomCrosHealthdDiagnosticRoutineEnum> GetAvailableRoutines()
      override;
  void RunBatteryCapacityRoutine(
      uint32_t low_mah,
      uint32_t high_mah,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count,
      uint32_t percent_battery_wear_allowed,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunUrandomRoutine(
      uint32_t length_seconds,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunSmartctlCheckRoutine(
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunAcPowerRoutine(
      MojomCrosHealthdAcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunCpuCacheRoutine(
      const base::TimeDelta& exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunCpuStressRoutine(
      const base::TimeDelta& exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunFloatingPointAccuracyRoutine(
      const base::TimeDelta& exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void GetRoutineUpdate(
      int32_t id,
      MojomCrosHealthdDiagnosticRoutineCommandEnum command,
      bool include_output,
      chromeos::cros_healthd::mojom::RoutineUpdate* response) override;

 private:
  void RunRoutine(
      std::unique_ptr<DiagnosticRoutine> routine,
      int32_t* id_out,
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status);

  // Map from IDs to instances of diagnostics routines that have
  // been started.
  std::map<int32_t, std::unique_ptr<DiagnosticRoutine>> active_routines_;
  // Generator for IDs - currently, when we need a new ID we just return
  // next_id_, then increment next_id_.
  int32_t next_id_ = 1;
  // Each of the supported diagnostic routines. Must be kept in sync with the
  // enums in diagnostics/mojo/cros_health_diagnostics.mojom.
  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
      available_routines_{
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kUrandom,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::
              kBatteryCapacity,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kBatteryHealth,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kSmartctlCheck,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kAcPower,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kCpuCache,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kCpuStress,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::
              kFloatingPointAccuracy,
          chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::kNvmeWearLevel};
  // Responsible for making async calls to debugd. Unowned pointer that should
  // outlive this instance.
  DebugdAdapter* debugd_adapter_ = nullptr;
  // Responsible for making the routines. Unowned pointer that should outlive
  // this instance.
  CrosHealthdRoutineFactory* routine_factory_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdRoutineServiceImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_
