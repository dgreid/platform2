// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LAN_CONNECTIVITY_LAN_CONNECTIVITY_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LAN_CONNECTIVITY_LAN_CONNECTIVITY_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

// Status messages reported by the LAN connectivity routine.
extern const char kLanConnectivityRoutineNoProblemMessage[];
extern const char kLanConnectivityRoutineProblemMessage[];
extern const char kLanConnectivityRoutineNotRunMessage[];

// Checks whether the device is connected to a LAN.
class LanConnectivityRoutine final : public DiagnosticRoutine {
 public:
  explicit LanConnectivityRoutine(
      NetworkDiagnosticsAdapter* network_diagnostics_adapter);
  LanConnectivityRoutine(const LanConnectivityRoutine&) = delete;
  LanConnectivityRoutine& operator=(const LanConnectivityRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~LanConnectivityRoutine() override;
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
  uint32_t CalculateProgressPercent();
  // Translates the LAN connectivity routine verdict to a
  // DiagnosticRoutineStatusEnum.
  void TranslateVerdictToStatus(
      chromeos::network_diagnostics::mojom::RoutineVerdict verdict);

  // Status of the routine, reported by GetStatus() or noninteractive routine
  // updates.
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  // Unowned pointer to NetworkDiagnosticsAdapter that should outlive this
  // instance.
  NetworkDiagnosticsAdapter* network_diagnostics_adapter_;
  // Details of the routine's status, reported in noninteractive status updates.
  std::string status_message_;
  // Must be the last class member.
  base::WeakPtrFactory<LanConnectivityRoutine> weak_ptr_factory_{this};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_LAN_CONNECTIVITY_LAN_CONNECTIVITY_H_
