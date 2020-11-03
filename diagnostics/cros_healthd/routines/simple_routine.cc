// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/simple_routine.h"

#include <cstdint>
#include <string>
#include <utility>

#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/common/mojo_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

uint32_t CalculateProgressPercent(
    mojo_ipc::DiagnosticRoutineStatusEnum status) {
  // Since simple routines cannot be cancelled, the progress percent can only be
  // 0 or 100.
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kError)
    return 100;
  return 0;
}

}  // namespace

SimpleRoutine::SimpleRoutine(Task task)
    : task_(std::move(task)),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {}

SimpleRoutine::~SimpleRoutine() = default;

void SimpleRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  std::move(task_).Run(&status_, &status_message_, &output_dict_);
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kPassed &&
      status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning) {
    LOG(ERROR) << base::StringPrintf(
        "Routine unsuccessful with status: %d and message: %s.", status_,
        status_message_.c_str());
  }
}

// Simple routines can only be started.
void SimpleRoutine::Resume() {}
void SimpleRoutine::Cancel() {}

void SimpleRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                         bool include_output) {
  // Because simple routines are non-interactive, we will never include a user
  // message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = CalculateProgressPercent(status_);

  if (include_output && !output_dict_.DictEmpty()) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        output_dict_, base::JSONWriter::Options::OPTIONS_PRETTY_PRINT, &json);
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(json));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum SimpleRoutine::GetStatus() {
  return status_;
}

}  // namespace diagnostics
