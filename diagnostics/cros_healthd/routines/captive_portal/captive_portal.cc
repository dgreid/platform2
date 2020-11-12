// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/captive_portal/captive_portal.h"

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

// Parses the results of the captive portal routine.
void ParseCaptivePortalResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::CaptivePortalProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kPortalRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kPortalRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::CaptivePortalProblem::kNoActiveNetworks:
          *status_message = kPortalRoutineNoActiveNetworksProblemMessage;
          break;
        case network_diagnostics_ipc::CaptivePortalProblem::kUnknownPortalState:
          *status_message = kPortalRoutineUnknownPortalStateProblemMessage;
          break;
        case network_diagnostics_ipc::CaptivePortalProblem::kPortalSuspected:
          *status_message = kPortalRoutinePortalSuspectedProblemMessage;
          break;
        case network_diagnostics_ipc::CaptivePortalProblem::kPortal:
          *status_message = kPortalRoutinePortalProblemMessage;
          break;
        case network_diagnostics_ipc::CaptivePortalProblem::kProxyAuthRequired:
          *status_message = kPortalRoutineProxyAuthRequiredProblemMessage;
          break;
        case network_diagnostics_ipc::CaptivePortalProblem::kNoInternet:
          *status_message = kPortalRoutineNoInternetProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the captive portal
// routine never includes an output.
void RunCaptivePortalRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunCaptivePortalRoutine(
      base::BindOnce(&ParseCaptivePortalResult, status, status_message));
}

}  // namespace

const char kPortalRoutineNoProblemMessage[] =
    "Captive portal routine passed with no problems.";
const char kPortalRoutineNoActiveNetworksProblemMessage[] =
    "No active networks found.";
const char kPortalRoutineUnknownPortalStateProblemMessage[] =
    "The active network is not connected or the portal state is not available.";
const char kPortalRoutinePortalSuspectedProblemMessage[] =
    "A portal is suspected but no redirect was provided.";
const char kPortalRoutinePortalProblemMessage[] =
    "The network is in a portal state with a redirect URL.";
const char kPortalRoutineProxyAuthRequiredProblemMessage[] =
    "A proxy requiring authentication is detected.";
const char kPortalRoutineNoInternetProblemMessage[] =
    "The active network is connected but no internet is available and no proxy "
    "was detected.";
const char kPortalRoutineNotRunMessage[] =
    "Captive portal routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateCaptivePortalRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunCaptivePortalRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
