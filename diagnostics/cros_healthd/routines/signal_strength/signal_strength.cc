// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>

#include "diagnostics/cros_healthd/routines/network_routine.h"
#include "diagnostics/cros_healthd/routines/signal_strength/signal_strength.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

}  // namespace

const char kSignalStrengthRoutineNoProblemMessage[] =
    "Signal strength routine passed with no problems.";
const char kSignalStrengthRoutineSignalNotFoundProblemMessage[] =
    "Signal not found.";
const char kSignalStrengthRoutineWeakSignalProblemMessage[] =
    "Weak signal detected.";
const char kSignalStrengthRoutineNotRunMessage[] =
    "Signal strength routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateSignalStrengthRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<
      NetworkRoutine<network_diagnostics_ipc::SignalStrengthProblem>>(
      network_diagnostics_adapter,
      mojo_ipc::DiagnosticRoutineEnum::kSignalStrength,
      base::BindOnce([](mojo_ipc::DiagnosticRoutineStatusEnum* status,
                        std::string* status_message,
                        network_diagnostics_ipc::RoutineVerdict verdict,
                        const std::vector<
                            network_diagnostics_ipc::SignalStrengthProblem>&
                            problems) {
        switch (verdict) {
          case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
            *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
            *status_message = kSignalStrengthRoutineNoProblemMessage;
            break;
          case network_diagnostics_ipc::RoutineVerdict::kProblem:
            *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
            switch (problems[0]) {
              case network_diagnostics_ipc::SignalStrengthProblem::
                  kSignalNotFound:
                *status_message =
                    kSignalStrengthRoutineSignalNotFoundProblemMessage;
                break;
              case network_diagnostics_ipc::SignalStrengthProblem::kWeakSignal:
                *status_message =
                    kSignalStrengthRoutineWeakSignalProblemMessage;
                break;
            }
            break;
          case network_diagnostics_ipc::RoutineVerdict::kNotRun:
            *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
            *status_message = kSignalStrengthRoutineNotRunMessage;
            break;
        }
      }));
}

}  // namespace diagnostics
