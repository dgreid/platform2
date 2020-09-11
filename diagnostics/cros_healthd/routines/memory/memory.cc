// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/memory/memory.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <re2/re2.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/memory/memory_constants.h"

namespace diagnostics {

namespace {

namespace executor_ipc = chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = chromeos::cros_healthd::mojom;

// Approximate number of microseconds per byte of memory tested. Derived from
// testing on a nami device.
constexpr double kMicrosecondsPerByte = 0.20;

}  // namespace

MemoryRoutine::MemoryRoutine(Context* context,
                             const base::TickClock* tick_clock)
    : context_(context),
      status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {
  DCHECK(context_);

  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

MemoryRoutine::~MemoryRoutine() = default;

void MemoryRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);

  // Estimate the routine's duration based on the amount of free memory.
  expected_duration_us_ =
      base::SysInfo::AmountOfAvailablePhysicalMemory() * kMicrosecondsPerByte;
  start_ticks_ = tick_clock_->NowTicks();

  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
  status_message_ = kMemoryRoutineRunningMessage;
  context_->executor()->RunMemtester(base::BindOnce(
      &MemoryRoutine::ParseMemtesterOutput, weak_ptr_factory_.GetWeakPtr()));
}

// The memory routine can only be started.
void MemoryRoutine::Resume() {}
void MemoryRoutine::Cancel() {}

void MemoryRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                         bool include_output) {
  DCHECK(response);

  // Because the memory routine is non-interactive, we will never include a user
  // message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());

  if (include_output) {
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(output_));
  }

  // If the routine has finished, set the progress percent to 100 and don't take
  // the amount of time ran into account.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    response->progress_percent = 100;
    return;
  }

  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kReady) {
    // The routine has not started.
    response->progress_percent = 0;
    return;
  }

  // Cap the progress at 99, in case it's taking longer than the estimated
  // time.
  base::TimeDelta elapsed_time = tick_clock_->NowTicks() - start_ticks_;
  response->progress_percent =
      std::min<int64_t>(99, static_cast<int64_t>(elapsed_time.InMicroseconds() /
                                                 expected_duration_us_ * 100));
}

mojo_ipc::DiagnosticRoutineStatusEnum MemoryRoutine::GetStatus() {
  return status_;
}

void MemoryRoutine::ParseMemtesterOutput(
    executor_ipc::ProcessResultPtr process) {
  // We'll give the full process output, regardless of result.
  output_ = process->out;

  int32_t ret = process->return_code;
  if (ret == EXIT_SUCCESS) {
    status_message_ = kMemoryRoutineSucceededMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
    return;
  }

  std::string status_message;
  if (ret & MemtesterErrorCodes::kAllocatingLockingInvokingError)
    status_message += kMemoryRoutineAllocatingLockingInvokingFailureMessage;

  if (ret & MemtesterErrorCodes::kStuckAddressTestError)
    status_message += kMemoryRoutineStuckAddressTestFailureMessage;

  if (ret & MemtesterErrorCodes::kOtherTestError)
    status_message += kMemoryRoutineOtherTestFailureMessage;

  status_message_ = std::move(status_message);
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
}

}  // namespace diagnostics
