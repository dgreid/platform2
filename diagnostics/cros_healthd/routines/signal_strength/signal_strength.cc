// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/signal_strength/signal_strength.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// Parses the results of the signal strength routine.
void ParseSignalStrengthResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::SignalStrengthProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kSignalStrengthRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::SignalStrengthProblem::kWeakSignal:
          *status_message = kSignalStrengthRoutineWeakSignalProblemMessage;
          break;
      }
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kSignalStrengthRoutineNotRunMessage;
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the signal strength
// routine never includes an output.
void RunSignalStrengthRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunSignalStrengthRoutine(
      base::BindOnce(&ParseSignalStrengthResult, status, status_message));
}

}  // namespace

const char kSignalStrengthRoutineNoProblemMessage[] =
    "Signal strength routine passed with no problems.";
const char kSignalStrengthRoutineWeakSignalProblemMessage[] =
    "Weak signal detected.";
const char kSignalStrengthRoutineNotRunMessage[] =
    "Signal strength routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateSignalStrengthRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunSignalStrengthRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
