// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_ROUTINE_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_ROUTINE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/routines/diag_routine.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

// A class for creating a network diagnostic routine.
template <typename T>
class NetworkRoutine final : public DiagnosticRoutine {
 public:
  NetworkRoutine(
      NetworkDiagnosticsAdapter* network_diagnostics_adapter,
      chromeos::cros_healthd::mojom::DiagnosticRoutineEnum
          diagnostic_routine_enum,
      base::OnceCallback<void(
          chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status,
          std::string* status_message,
          chromeos::network_diagnostics::mojom::RoutineVerdict,
          const std::vector<T>& problems)>
          translate_verdict_and_problems_to_status_callback)
      : network_diagnostics_adapter_(network_diagnostics_adapter),
        diagnostic_routine_enum_(diagnostic_routine_enum),
        status_(chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
                    kReady) {
    DCHECK(network_diagnostics_adapter_);
    translate_verdict_and_problems_to_status_callback_ =
        std::move(translate_verdict_and_problems_to_status_callback);
  }

  NetworkRoutine(const NetworkRoutine&) = delete;
  NetworkRoutine& operator=(const NetworkRoutine&) = delete;
  ~NetworkRoutine() override = default;

  // DiagnosticRoutine overrides:
  void Start() override {
    DCHECK_EQ(
        status_,
        chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kReady);
    status_ =
        chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kRunning;
    switch (diagnostic_routine_enum_) {
      case chromeos::cros_healthd::mojom::DiagnosticRoutineEnum::
          kSignalStrength:
        network_diagnostics_adapter_->RunSignalStrengthRoutine(base::BindOnce(
            std::move(translate_verdict_and_problems_to_status_callback_),
            &status_, &status_message_));
        return;
      default:
        status_ =
            chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kError;
        status_message_ =
            "Unsupported network routine: " +
            base::NumberToString(static_cast<int>(diagnostic_routine_enum_));
        LOG(ERROR) << status_message_;
        return;
    }
  }

  // Network routines can only be started.
  void Resume() override {}
  void Cancel() override {}

  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override {
    // Because the network routines are non-interactive, we will never include a
    // user message.
    chromeos::cros_healthd::mojom::NonInteractiveRoutineUpdate update;
    update.status = status_;
    update.status_message = status_message_;

    response->routine_update_union->set_noninteractive_update(update.Clone());
    response->progress_percent = CalculateProgressPercent();
  }

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override {
    return status_;
  }

 private:
  uint32_t CalculateProgressPercent() {
    // Since network routines cannot be cancelled, the progress percent can only
    // be 0 or 100.
    if (status_ == chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
                       kPassed ||
        status_ == chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::
                       kFailed ||
        status_ ==
            chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum::kError)
      return 100;
    return 0;
  }

  // Unowned pointer that should outlive this instance
  NetworkDiagnosticsAdapter* network_diagnostics_adapter_;
  // Type of network routine.
  chromeos::cros_healthd::mojom::DiagnosticRoutineEnum diagnostic_routine_enum_;
  // Status of the routine, reported by GetStatus() or noninteractive routine
  // updates.
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  // Details of the routine's status, reported in noninteractive status updates.
  std::string status_message_;
  // Callback that is invoked if a verdict and problems have been determined.
  base::OnceCallback<void(
      chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum* status,
      std::string* status_message,
      chromeos::network_diagnostics::mojom::RoutineVerdict,
      const std::vector<T>& problems)>
      translate_verdict_and_problems_to_status_callback_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_NETWORK_ROUTINE_H_
