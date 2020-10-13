// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/has_secure_wifi_connection/has_secure_wifi_connection.h"

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

// Parses the results of the has secure WiFi connection routine.
void ParseHasSecureWiFiConnectionResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::HasSecureWiFiConnectionProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kHasSecureWiFiConnectionRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
      *status_message = kHasSecureWiFiConnectionRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
            kSecurityTypeNone:
          *status_message =
              kHasSecureWiFiConnectionRoutineSecurityTypeNoneProblemMessage;
          break;
        case network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
            kSecurityTypeWep8021x:
          *status_message =
              kHasSecureWiFiConnectionRoutineSecurityTypeWep8021xProblemMessage;
          break;
        case network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
            kSecurityTypeWepPsk:
          *status_message =
              kHasSecureWiFiConnectionRoutineSecurityTypeWepPskProblemMessage;
          break;
        case network_diagnostics_ipc::HasSecureWiFiConnectionProblem::
            kUnknownSecurityType:
          *status_message =
              kHasSecureWiFiConnectionRoutineUnknownSecurityTypeProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the has secure WiFi
// connection routine never includes an output.
void RunHasSecureWiFiConnectionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    std::string* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunHasSecureWiFiConnectionRoutine(base::BindOnce(
      &ParseHasSecureWiFiConnectionResult, status, status_message));
}

}  // namespace

const char kHasSecureWiFiConnectionRoutineNoProblemMessage[] =
    "Has secure WiFi connection routine passed with no problems.";
const char kHasSecureWiFiConnectionRoutineSecurityTypeNoneProblemMessage[] =
    "No security type found.";
const char kHasSecureWiFiConnectionRoutineSecurityTypeWep8021xProblemMessage[] =
    "Insecure security type Wep8021x found.";
const char kHasSecureWiFiConnectionRoutineSecurityTypeWepPskProblemMessage[] =
    "Insecure security type WepPsk found.";
const char kHasSecureWiFiConnectionRoutineUnknownSecurityTypeProblemMessage[] =
    "Unknown security type found.";
const char kHasSecureWiFiConnectionRoutineNotRunMessage[] =
    "Has secure WiFi connection routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHasSecureWiFiConnectionRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(base::BindOnce(
      &RunHasSecureWiFiConnectionRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
