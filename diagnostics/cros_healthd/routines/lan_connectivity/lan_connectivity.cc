// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/lan_connectivity/lan_connectivity.h"

#include <string>

#include <base/bind.h>
#include <base/logging.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/network_diagnostics/network_diagnostics_adapter.h"
#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// Parses the results of the LAN connectivity routine.
void ParseLanConnectivityResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kLanConnectivityRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      *status_message = kLanConnectivityRoutineProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kLanConnectivityRoutineNotRunMessage;
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the LAN connectivity
// routine never includes an output.
void RunLanConnectivityRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunLanConnectivityRoutine(
      base::BindOnce(&ParseLanConnectivityResult, status, status_message));
}

}  // namespace

const char kLanConnectivityRoutineNoProblemMessage[] =
    "LAN Connectivity routine passed with no problems.";
const char kLanConnectivityRoutineProblemMessage[] =
    "No LAN Connectivity detected.";
const char kLanConnectivityRoutineNotRunMessage[] =
    "LAN Connectivity routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateLanConnectivityRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunLanConnectivityRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
