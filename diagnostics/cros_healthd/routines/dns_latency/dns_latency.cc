// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/dns_latency/dns_latency.h"

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

// Parses the results of the DNS latency routine.
void ParseDnsLatencyResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::DnsLatencyProblem>& problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kDnsLatencyRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
      *status_message = kDnsLatencyRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::DnsLatencyProblem::kHostResolutionFailure:
          *status_message =
              kDnsLatencyRoutineHostResolutionFailureProblemMessage;
          break;
        case network_diagnostics_ipc::DnsLatencyProblem::
            kSlightlyAboveThreshold:
          *status_message =
              kDnsLatencyRoutineSlightlyAboveThresholdProblemMessage;
          break;
        case network_diagnostics_ipc::DnsLatencyProblem::
            kSignificantlyAboveThreshold:
          *status_message =
              kDnsLatencyRoutineSignificantlyAboveThresholdProblemMessage;
          break;
      }
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the DNS latency
// routine never includes an output.
void RunDnsLatencyRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunDnsLatencyRoutine(
      base::BindOnce(&ParseDnsLatencyResult, status, status_message));
}

}  // namespace

const char kDnsLatencyRoutineNoProblemMessage[] =
    "DNS latency routine passed with no problems.";
const char kDnsLatencyRoutineHostResolutionFailureProblemMessage[] =
    "Failed to resolve one or more hosts.";
const char kDnsLatencyRoutineSlightlyAboveThresholdProblemMessage[] =
    "Average DNS latency across hosts is slightly above expected threshold.";
const char kDnsLatencyRoutineSignificantlyAboveThresholdProblemMessage[] =
    "Average DNS latency across hosts is significantly above expected "
    "threshold.";
const char kDnsLatencyRoutineNotRunMessage[] =
    "DNS latency routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateDnsLatencyRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunDnsLatencyRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
