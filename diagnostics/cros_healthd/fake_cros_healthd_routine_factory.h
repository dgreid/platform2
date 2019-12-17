// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/macros.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/cros_healthd_routine_factory.h"
#include "diagnostics/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Implementation of CrosHealthdRoutineFactory that should only be used for
// testing.
class FakeCrosHealthdRoutineFactory final : public CrosHealthdRoutineFactory {
 public:
  FakeCrosHealthdRoutineFactory();
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
      const base::TimeDelta& exec_duration) override;
  std::unique_ptr<DiagnosticRoutine> MakeCpuStressRoutine(
      const base::TimeDelta& exec_duration) override;

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

  DISALLOW_COPY_AND_ASSIGN(FakeCrosHealthdRoutineFactory);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FAKE_CROS_HEALTHD_ROUTINE_FACTORY_H_
