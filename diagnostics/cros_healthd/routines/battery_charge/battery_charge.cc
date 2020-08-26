// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge.h"

#include <inttypes.h>

#include <cstdint>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_charge/battery_charge_constants.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

BatteryChargeRoutine::BatteryChargeRoutine(
    base::TimeDelta exec_duration,
    uint32_t minimum_charge_percent_required,
    const base::FilePath& root_dir,
    const base::TickClock* tick_clock)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      exec_duration_(exec_duration),
      minimum_charge_percent_required_(minimum_charge_percent_required),
      root_dir_(root_dir) {
  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

BatteryChargeRoutine::~BatteryChargeRoutine() = default;

void BatteryChargeRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can plug in the charger if necessary.
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  CalculateProgressPercent();
}

void BatteryChargeRoutine::Resume() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  status_ = RunBatteryChargeRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

void BatteryChargeRoutine::Cancel() {
  // Cancel the routine if it hasn't already finished.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return;
  }

  CalculateProgressPercent();

  callback_.Cancel();
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
  status_message_ = kBatteryChargeRoutineCancelledMessage;
}

void BatteryChargeRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    mojo_ipc::InteractiveRoutineUpdate interactive_update;
    interactive_update.user_message =
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower;
    response->routine_update_union->set_interactive_update(
        interactive_update.Clone());
  } else {
    mojo_ipc::NonInteractiveRoutineUpdate noninteractive_update;
    noninteractive_update.status = status_;
    noninteractive_update.status_message = status_message_;

    response->routine_update_union->set_noninteractive_update(
        noninteractive_update.Clone());
  }

  CalculateProgressPercent();
  response->progress_percent = progress_percent_;
  if (include_output) {
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(output_));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum BatteryChargeRoutine::GetStatus() {
  return status_;
}

void BatteryChargeRoutine::CalculateProgressPercent() {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    // The routine has finished, so report 100.
    progress_percent_ = 100;
  } else if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kError &&
             status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled &&
             start_ticks_.has_value()) {
    progress_percent_ =
        100 * (tick_clock_->NowTicks() - start_ticks_.value()) / exec_duration_;
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum
BatteryChargeRoutine::RunBatteryChargeRoutine() {
  base::FilePath battery_path = root_dir_.Append(kBatteryDirectoryPath);

  std::string status;
  if (!ReadAndTrimString(battery_path, kBatteryStatusFileName, &status)) {
    status_message_ =
        kBatteryChargeRoutineFailedReadingBatteryAttributesMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  if (status != kBatteryStatusChargingValue) {
    status_message_ = kBatteryChargeRoutineNotChargingMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::Optional<uint32_t> beginning_charge_percent =
      CalculateBatteryChargePercent(root_dir_);
  if (!beginning_charge_percent.has_value()) {
    status_message_ =
        kBatteryChargeRoutineFailedReadingBatteryAttributesMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  uint32_t beginning_charge_percent_value = beginning_charge_percent.value();
  if (beginning_charge_percent_value + minimum_charge_percent_required_ > 100) {
    status_message_ = kBatteryChargeRoutineInvalidParametersMessage;
    output_ = base::StringPrintf("Battery is at %d%%, and cannot charge %d%%.",
                                 beginning_charge_percent_value,
                                 minimum_charge_percent_required_);
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  start_ticks_ = tick_clock_->NowTicks();

  callback_.Reset(base::Bind(&BatteryChargeRoutine::DetermineRoutineResult,
                             weak_ptr_factory_.GetWeakPtr(),
                             beginning_charge_percent_value));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, callback_.callback(), exec_duration_);

  status_message_ = kBatteryChargeRoutineRunningMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
}

void BatteryChargeRoutine::DetermineRoutineResult(
    uint32_t beginning_charge_percent) {
  base::Optional<uint32_t> ending_charge_percent =
      CalculateBatteryChargePercent(root_dir_);
  if (!ending_charge_percent.has_value()) {
    status_message_ =
        kBatteryChargeRoutineFailedReadingBatteryAttributesMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryChargeRoutineFailedReadingBatteryAttributesMessage;
    return;
  }

  uint32_t ending_charge_percent_value = ending_charge_percent.value();
  if (ending_charge_percent_value < beginning_charge_percent) {
    status_message_ = kBatteryChargeRoutineNotChargingMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryChargeRoutineNotChargingMessage;
    return;
  }

  uint32_t charge_percent =
      ending_charge_percent_value - beginning_charge_percent;
  output_ = base::StringPrintf(
      "Battery charged from %d%% to %d%% in %" PRId64 " seconds.",
      beginning_charge_percent, ending_charge_percent_value,
      exec_duration_.InSeconds());
  if (charge_percent < minimum_charge_percent_required_) {
    status_message_ = kBatteryChargeRoutineFailedInsufficientChargeMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return;
  }

  status_message_ = kBatteryChargeRoutineSucceededMessage;
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
