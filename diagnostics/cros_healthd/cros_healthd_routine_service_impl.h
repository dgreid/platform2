// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/optional.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/cros_healthd_routine_service.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd.mojom.h"

namespace diagnostics {

// Production implementation of the CrosHealthdRoutineService interface.
class CrosHealthdRoutineServiceImpl final : public CrosHealthdRoutineService {
 public:
  CrosHealthdRoutineServiceImpl(Context* context,
                                CrosHealthdRoutineFactory* routine_factory);
  CrosHealthdRoutineServiceImpl(const CrosHealthdRoutineServiceImpl&) = delete;
  CrosHealthdRoutineServiceImpl& operator=(
      const CrosHealthdRoutineServiceImpl&) = delete;
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
      base::TimeDelta exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunCpuStressRoutine(
      base::TimeDelta exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunFloatingPointAccuracyRoutine(
      base::TimeDelta exec_duration,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunNvmeSelfTestRoutine(
      MojomCrosHealthdNvmeSelfTestTypeEnum nvme_self_test_type,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunPrimeSearchRoutine(
      base::TimeDelta exec_duration,
      uint64_t max_num,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required,
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunMemoryRoutine(
      int32_t* id,
      MojomCrosHealthdDiagnosticRoutineStatusEnum* status) override;
  void RunLanConnectivityRoutine(
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
      chromeos::cros_healthd::mojom::DiagnosticRoutineEnum routine_enum,
      int32_t* id_out,
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status);

  // Checks what routines are supported on the device and populates the member
  // available_routines_.
  void PopulateAvailableRoutines();

  // Map from IDs to instances of diagnostics routines that have
  // been started.
  std::map<int32_t, std::unique_ptr<DiagnosticRoutine>> active_routines_;
  // Generator for IDs - currently, when we need a new ID we just return
  // next_id_, then increment next_id_.
  int32_t next_id_ = 1;
  // Each of the supported diagnostic routines. Must be kept in sync with the
  // enums in diagnostics/mojo/cros_health_diagnostics.mojom.
  std::set<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
      available_routines_;
  // Unowned pointer that should outlive this instance.
  Context* const context_ = nullptr;
  // Responsible for making the routines. Unowned pointer that should outlive
  // this instance.
  CrosHealthdRoutineFactory* routine_factory_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_CROS_HEALTHD_ROUTINE_SERVICE_IMPL_H_
