// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/gateway_can_be_pinged/gateway_can_be_pinged.h"

#include <memory>
#include <string>
#include <vector>

#include <base/bind.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/network_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

// Parses the results of the gateway can be pinged routine.
void ParseGatewayCanBePingedResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::GatewayCanBePingedProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kPingRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
      *status_message = kPingRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::GatewayCanBePingedProblem::
            kUnreachableGateway:
          *status_message = kPingRoutineUnreachableGatewayProblemMessage;
          break;
        case network_diagnostics_ipc::GatewayCanBePingedProblem::
            kFailedToPingDefaultNetwork:
          *status_message = kPingRoutineFailedPingProblemMessage;
          break;
        case network_diagnostics_ipc::GatewayCanBePingedProblem::
            kDefaultNetworkAboveLatencyThreshold:
          *status_message = kPingRoutineHighPingLatencyProblemMessage;
          break;
        case network_diagnostics_ipc::GatewayCanBePingedProblem::
            kUnsuccessfulNonDefaultNetworksPings:
          *status_message = kPingRoutineFailedNonDefaultPingsProblemMessage;
          break;
        case network_diagnostics_ipc::GatewayCanBePingedProblem::
            kNonDefaultNetworksAboveLatencyThreshold:
          *status_message = kPingRoutineNonDefaultHighLatencyProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the gateway can be pinged
// routine never includes an output.
void RunGatewayCanBePingedRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    std::string* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunGatewayCanBePingedRoutine(
      base::BindOnce(&ParseGatewayCanBePingedResult, status, status_message));
}

}  // namespace

const char kPingRoutineNoProblemMessage[] =
    "Gateway can be pinged routine passed with no problems.";
const char kPingRoutineUnreachableGatewayProblemMessage[] =
    "All gateways are unreachable, hence cannot be pinged.";
const char kPingRoutineFailedPingProblemMessage[] =
    "The default network cannot be pinged.";
const char kPingRoutineHighPingLatencyProblemMessage[] =
    "The default network has a latency above the threshold.";
const char kPingRoutineFailedNonDefaultPingsProblemMessage[] =
    "One or more of the non-default networks has failed pings.";
const char kPingRoutineNonDefaultHighLatencyProblemMessage[] =
    "One or more of the non-default networks has a latency above the "
    "threshold.";
const char kPingRoutineNotRunMessage[] =
    "Gateway can be pinged routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateGatewayCanBePingedRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(base::BindOnce(
      &RunGatewayCanBePingedRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
