// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/http_firewall/http_firewall.h"

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

// Parses the results of the HTTP firewall routine.
void ParseHttpFirewallResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::HttpFirewallProblem>& problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kHttpFirewallRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
      *status_message = kHttpFirewallRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::HttpFirewallProblem::
            kDnsResolutionFailuresAboveThreshold:
          *status_message =
              kHttpFirewallRoutineHighDnsResolutionFailureRateProblemMessage;
          break;
        case network_diagnostics_ipc::HttpFirewallProblem::kFirewallDetected:
          *status_message = kHttpFirewallRoutineFirewallDetectedProblemMessage;
          break;
        case network_diagnostics_ipc::HttpFirewallProblem::kPotentialFirewall:
          *status_message = kHttpFirewallRoutinePotentialFirewallProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the HTTP firewall routine
// never includes an output.
void RunHttpFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunHttpFirewallRoutine(
      base::BindOnce(&ParseHttpFirewallResult, status, status_message));
}

}  // namespace

const char kHttpFirewallRoutineNoProblemMessage[] =
    "HTTP firewall routine passed with no problems.";
const char kHttpFirewallRoutineHighDnsResolutionFailureRateProblemMessage[] =
    "DNS resolution failures above threshold.";
const char kHttpFirewallRoutineFirewallDetectedProblemMessage[] =
    "Firewall detected.";
const char kHttpFirewallRoutinePotentialFirewallProblemMessage[] =
    "A firewall may potentially exist.";
const char kHttpFirewallRoutineNotRunMessage[] =
    "HTTP firewall routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpFirewallRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpFirewallRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
