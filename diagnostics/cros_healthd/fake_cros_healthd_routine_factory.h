// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/optional.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Implementation of CrosHealthdRoutineFactory that should only be used for
// testing.
class FakeCrosHealthdRoutineFactory final : public CrosHealthdRoutineFactory {
 public:
  FakeCrosHealthdRoutineFactory();
  FakeCrosHealthdRoutineFactory(const FakeCrosHealthdRoutineFactory&) = delete;
  FakeCrosHealthdRoutineFactory& operator=(
      const FakeCrosHealthdRoutineFactory&) = delete;
  ~FakeCrosHealthdRoutineFactory() override;

  // Sets the number of times that Start(), Resume(), and Cancel() are expected
  // to be called on the next routine to be created. If this function isn't
  // called before calling MakeSomeRoutine, then the created routine will not
  // count the expected function calls. Any future calls to this function will
  // override the settings from a previous call. Must be called before
  // SetNonInteractiveStatus.
  void SetRoutineExpectations(int num_expected_start_calls,
                              int num_expected_resume_calls,
                              int num_expected_cancel_calls);

  // Makes the next routine returned by MakeSomeRoutine report a noninteractive
  // status with the specified status, status_message, progress_percent and
  // output. Any future calls to this function will override the settings from a
  // previous call.
  void SetNonInteractiveStatus(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status,
      const std::string& status_message,
      uint32_t progress_percent,
      const std::string& output);

  // CrosHealthdRoutineFactory overrides:
  std::unique_ptr<DiagnosticRoutine> MakeUrandomRoutine(
      uint32_t length_seconds) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryCapacityRoutine(
      uint32_t low_mah, uint32_t high_mah) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryHealthRoutine(
      uint32_t maximum_cycle_count,
      uint32_t percent_battery_wear_allowed) override;
  std::unique_ptr<DiagnosticRoutine> MakeSmartctlCheckRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type) override;
  std::unique_ptr<DiagnosticRoutine> MakeCpuCacheRoutine(
      base::TimeDelta exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeCpuStressRoutine(
      base::TimeDelta exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeFloatingPointAccuracyRoutine(
      base::TimeDelta exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeNvmeWearLevelRoutine(
      DebugdAdapter* debugd_adapter, uint32_t wear_level_threshold) override;
  std::unique_ptr<DiagnosticRoutine> MakeNvmeSelfTestRoutine(
      DebugdAdapter* debugd_adapter,
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type)
      override;
  std::unique_ptr<DiagnosticRoutine> MakeDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb) override;
  std::unique_ptr<DiagnosticRoutine> MakePrimeSearchRoutine(
      base::TimeDelta exec_duration, uint64_t max_num) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryDischargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t maximum_discharge_percent_allowed) override;
  std::unique_ptr<DiagnosticRoutine> MakeBatteryChargeRoutine(
      base::TimeDelta exec_duration,
      uint32_t minimum_charge_percent_required) override;
  std::unique_ptr<DiagnosticRoutine> MakeMemoryRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeLanConnectivityRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeSignalStrengthRoutine() override;
  std::unique_ptr<DiagnosticRoutine> MakeGatewayCanBePingedRoutine() override;

 private:
  // The routine that will be returned by any calls to MakeSomeRoutine.
  std::unique_ptr<DiagnosticRoutine> next_routine_;
  // Number of times that any created routines expect their Start() method to be
  // called.
  int num_expected_start_calls_;
  // Number of times that any created routines expect their Resume() method to
  // be called.
  int num_expected_resume_calls_;
  // Number of times that any created routines expect their Cancel() method to
  // be called.
  int num_expected_cancel_calls_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_
