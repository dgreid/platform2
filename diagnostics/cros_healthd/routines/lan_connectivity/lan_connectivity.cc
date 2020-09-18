// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/bind.h>
#include <base/logging.h>

#include "diagnostics/cros_healthd/routines/lan_connectivity/lan_connectivity.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

}  // namespace

const char kLanConnectivityRoutineNoProblemMessage[] =
    "LAN Connectivity routine passed with no problems.";
const char kLanConnectivityRoutineProblemMessage[] =
    "No LAN Connectivity detected.";
const char kLanConnectivityRoutineNotRunMessage[] =
    "LAN Connectivity routine did not run.";

LanConnectivityRoutine::LanConnectivityRoutine(
    NetworkDiagnosticsAdapter* network_diagnostics_adapter)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      network_diagnostics_adapter_(network_diagnostics_adapter) {
  DCHECK(network_diagnostics_adapter_);
}

LanConnectivityRoutine::~LanConnectivityRoutine() = default;

void LanConnectivityRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  network_diagnostics_adapter_->RunLanConnectivityRoutine(
      base::BindOnce(&LanConnectivityRoutine::TranslateVerdictToStatus,
                     weak_ptr_factory_.GetWeakPtr()));
}

// The LAN connectivity routine can only be started.
void LanConnectivityRoutine::Resume() {}
void LanConnectivityRoutine::Cancel() {}

void LanConnectivityRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  // Because the LAN connectivity routine is non-interactive, we will never
  // include a user message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = CalculateProgressPercent();
}

mojo_ipc::DiagnosticRoutineStatusEnum LanConnectivityRoutine::GetStatus() {
  return status_;
}

uint32_t LanConnectivityRoutine::CalculateProgressPercent() {
  // Since the LAN connectivity routine cannot be cancelled, the progress
  // percent can only be 0 or 100.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return 100;
  }
  return 0;
}

void LanConnectivityRoutine::TranslateVerdictToStatus(
    network_diagnostics_ipc::RoutineVerdict verdict) {
  switch (verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
      status_message_ = kLanConnectivityRoutineNoProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
      status_message_ = kLanConnectivityRoutineProblemMessage;
      break;
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
      status_message_ = kLanConnectivityRoutineNotRunMessage;
      break;
  }
}

}  // namespace diagnostics
