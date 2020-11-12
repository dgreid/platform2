// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/dns_resolver_present/dns_resolver_present.h"

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

// Parses the results of the DNS resolver present routine.
void ParseDnsResolverPresentResult(
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    network_diagnostics_ipc::RoutineVerdict verdict,
    const std::vector<network_diagnostics_ipc::DnsResolverPresentProblem>&
        problems) {
  DCHECK(status);
  DCHECK(status_message);

  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      *status_message = kDnsResolverPresentRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kNotRun;
      *status_message = kDnsResolverPresentRoutineNotRunMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      DCHECK(!problems.empty());
      switch (problems[0]) {
        case network_diagnostics_ipc::DnsResolverPresentProblem::
            kNoNameServersFound:
          *status_message =
              kDnsResolverPresentRoutineNoNameServersFoundProblemMessage;
          break;
        case network_diagnostics_ipc::DnsResolverPresentProblem::
            kMalformedNameServers:
          *status_message =
              kDnsResolverPresentRoutineMalformedNameServersProblemMessage;
          break;
        case network_diagnostics_ipc::DnsResolverPresentProblem::
            kEmptyNameServers:
          *status_message =
              kDnsResolverPresentRoutineEmptyNameServersProblemMessage;
          break;
      }
      break;
  }
}

// We include |output_dict| here to satisfy SimpleRoutine - the DNS resolver
// present routine never includes an output.
void RunDnsResolverPresentRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter,
    mojo_ipc::DiagnosticRoutineStatusEnum* status,
    std::string* status_message,
    base::Value* output_dict) {
  DCHECK(network_diagnostics_adapter);
  DCHECK(status);

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;

  network_diagnostics_adapter->RunDnsResolverPresentRoutine(
      base::BindOnce(&ParseDnsResolverPresentResult, status, status_message));
}

}  // namespace

const char kDnsResolverPresentRoutineNoProblemMessage[] =
    "DNS resolver present routine passed with no problems.";
const char kDnsResolverPresentRoutineNoNameServersFoundProblemMessage[] =
    "IP config has no list of name servers available.";
const char kDnsResolverPresentRoutineMalformedNameServersProblemMessage[] =
    "IP config has a list of at least one malformed name server.";
const char kDnsResolverPresentRoutineEmptyNameServersProblemMessage[] =
    "IP config has an empty list of name servers";
const char kDnsResolverPresentRoutineNotRunMessage[] =
    "DNS resolver present routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateDnsResolverPresentRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter) {
  return std::make_unique<SimpleRoutine>(base::BindOnce(
      &RunDnsResolverPresentRoutine, network_diagnostics_adapter));
}

}  // namespace diagnostics
