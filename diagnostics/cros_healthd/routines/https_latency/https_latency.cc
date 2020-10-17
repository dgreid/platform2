// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/https_latency/https_latency.h"

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

// Parses the results of the HTTPS latency routine.
void ParseHttpsLatencyResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::HttpsLatencyProblem>& problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kHttpsLatencyRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kHttpsLatencyRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::HttpsLatencyProblem::
            kFailedDnsResolutions:
          *status_message =
              kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage;
          break;
        case network_diagnostics_ipc::HttpsLatencyProblem::kFailedHttpsRequests:
          *status_message =
              kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage;
          break;
        case network_diagnostics_ipc::HttpsLatencyProblem::kHighLatency:
          *status_message = kHttpsLatencyRoutineHighLatencyProblemMessage;
          break;
        case network_diagnostics_ipc::HttpsLatencyProblem::kVeryHighLatency:
          *status_message = kHttpsLatencyRoutineVeryHighLatencyProblemMessage;
          break;
      }
      break;
  }
}

// We include |output| here to satisfy SimpleRoutine - the HTTPS latency
// routine never includes an output.
void RunHttpsLatencyRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunHttpsLatencyRoutine(
      base::BindOnce(&ParseHttpsLatencyResult, status, status_message));
}

}  // namespace

const char kHttpsLatencyRoutineNoProblemMessage[] =
    "HTTPS latency routine passed with no problems.";
const char kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage[] =
    "One or more DNS resolutions resulted in a failure.";
const char kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage[] =
    "One or more HTTPS requests resulted in a failure.";
const char kHttpsLatencyRoutineHighLatencyProblemMessage[] =
    "HTTPS request latency is high.";
const char kHttpsLatencyRoutineVeryHighLatencyProblemMessage[] =
    "HTTPS request latency is very high.";
const char kHttpsLatencyRoutineNotRunMessage[] =
    "HTTPS latency routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpsLatencyRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpsLatencyRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
