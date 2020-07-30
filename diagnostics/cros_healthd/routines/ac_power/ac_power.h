// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AC_POWER_AC_POWER_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AC_POWER_AC_POWER_H_

#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

// Status messages reported by the AC power routine.
extern const char kAcPowerRoutineSucceededMessage[];
extern const char kAcPowerRoutineFailedNotOnlineMessage[];
extern const char kAcPowerRoutineFailedNotOfflineMessage[];
extern const char kAcPowerRoutineFailedMismatchedPowerTypesMessage[];
extern const char kAcPowerRoutineNoValidPowerSupplyMessage[];
extern const char kAcPowerRoutineCancelledMessage[];

// Progress percent reported when the routine is in the waiting state.
extern const uint32_t kAcPowerRoutineWaitingProgressPercent;

// Checks the status of the power supply and optionally checks to see if the
// type of the power supply matches the power_type argument.
class AcPowerRoutine final : public DiagnosticRoutine {
 public:
  // Override |root_dir| for testing only.
  AcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type,
      const base::FilePath& root_dir = base::FilePath("/"));
  AcPowerRoutine(const AcPowerRoutine&) = delete;
  AcPowerRoutine& operator=(const AcPowerRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~AcPowerRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override;

 private:
  // Calculates the progress percent based on the current status.
  void CalculateProgressPercent();
  // Checks the machine state against the input parameters.
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum
  RunAcPowerRoutine();

  // Status of the routine, reported by GetStatus() or noninteractive routine
  // updates.
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  // Expected status of the power supply.
  chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_power_status_;
  // Expected type of the power supply.
  base::Optional<std::string> expected_power_type_;
  // Details of the routine's status, reported in noninteractive status updates.
  std::string status_message_;
  // Root directory appended to relative paths used by the routine.
  base::FilePath root_dir_;
  // A measure of how far along the routine is, reported in all status updates.
  uint32_t progress_percent_ = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_AC_POWER_AC_POWER_H_
