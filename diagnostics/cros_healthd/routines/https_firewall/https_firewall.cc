// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/https_firewall/https_firewall.h"

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

// Parses the results of the HTTPS firewall routine.
void ParseHttpsFirewallResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::HttpsFirewallProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kHttpsFirewallRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kHttpsFirewallRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::HttpsFirewallProblem::
            kHighDnsResolutionFailureRate:
          *status_message =
              kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage;
          break;
        case network_diagnostics_ipc::HttpsFirewallProblem::kFirewallDetected:
          *status_message = kHttpsFirewallRoutineFirewallDetectedProblemMessage;
          break;
        case network_diagnostics_ipc::HttpsFirewallProblem::kPotentialFirewall:
          *status_message =
              kHttpsFirewallRoutinePotentialFirewallProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the HTTPS firewall
// routine never includes an output.
void RunHttpsFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunHttpsFirewallRoutine(
      base::BindOnce(&ParseHttpsFirewallResult, status, status_message));
}

}  // namespace

const char kHttpsFirewallRoutineNoProblemMessage[] =
    "HTTPS firewall routine passed with no problems.";
const char kHttpsFirewallRoutineHighDnsResolutionFailureRateProblemMessage[] =
    "DNS resolution failure rate is high.";
const char kHttpsFirewallRoutineFirewallDetectedProblemMessage[] =
    "Firewall detected.";
const char kHttpsFirewallRoutinePotentialFirewallProblemMessage[] =
    "A firewall may potentially exist.";
const char kHttpsFirewallRoutineNotRunMessage[] =
    "HTTPS firewall routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpsFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpsFirewallRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
