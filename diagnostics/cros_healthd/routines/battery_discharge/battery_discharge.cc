// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge.h"

#include <inttypes.h>

#include <cmath>
#include <cstdint>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>

#include "diagnostics/common/mojo_utils.h"
#include "diagnostics/cros_healthd/routines/battery_discharge/battery_discharge_constants.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Calculates the charge percent of the battery. Returns true and populates
// |charge_percent_out| iff the battery charge percent was able to be
// calculated.
base::Optional<uint32_t> CalculateBatteryChargePercent(
    const base::FilePath& root_dir) {
  base::FilePath battery_path = root_dir.Append(kBatteryDirectoryPath);

  uint32_t charge_now;
  if (!ReadInteger(battery_path, kBatteryChargeNowFileName, base::StringToUint,
                   &charge_now)) {
    return base::nullopt;
  }

  uint32_t charge_full;
  if (!ReadInteger(battery_path, kBatteryChargeFullFileName, base::StringToUint,
                   &charge_full)) {
    return base::nullopt;
  }

  return static_cast<uint32_t>(
      std::round(100.0 * (static_cast<float>(charge_now) /
                          static_cast<float>(charge_full))));
}

}  // namespace

BatteryDischargeRoutine::BatteryDischargeRoutine(
    base::TimeDelta exec_duration,
    uint32_t maximum_discharge_percent_allowed,
    const base::FilePath& root_dir,
    const base::TickClock* tick_clock)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      exec_duration_(exec_duration),
      maximum_discharge_percent_allowed_(maximum_discharge_percent_allowed),
      root_dir_(root_dir) {
  if (tick_clock) {
    tick_clock_ = tick_clock;
  } else {
    default_tick_clock_ = std::make_unique<base::DefaultTickClock>();
    tick_clock_ = default_tick_clock_.get();
  }
  DCHECK(tick_clock_);
}

BatteryDischargeRoutine::~BatteryDischargeRoutine() = default;

void BatteryDischargeRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can unplug the charger if necessary.
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  CalculateProgressPercent();
}

void BatteryDischargeRoutine::Resume() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  status_ = RunBatteryDischargeRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kRunning)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

void BatteryDischargeRoutine::Cancel() {
  // Cancel the routine if it hasn't already finished.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed ||
      status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kError) {
    return;
  }

  CalculateProgressPercent();

  callback_.Cancel();
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
  status_message_ = kBatteryDischargeRoutineCancelledMessage;
}

void BatteryDischargeRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    mojo_ipc::InteractiveRoutineUpdate interactive_update;
    interactive_update.user_message =
        mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower;
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

mojo_ipc::DiagnosticRoutineStatusEnum BatteryDischargeRoutine::GetStatus() {
  return status_;
}

void BatteryDischargeRoutine::CalculateProgressPercent() {
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
BatteryDischargeRoutine::RunBatteryDischargeRoutine() {
  if (maximum_discharge_percent_allowed_ > 100) {
    status_message_ = kBatteryDischargeRoutineInvalidParametersMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::FilePath battery_path = root_dir_.Append(kBatteryDirectoryPath);

  std::string status;
  if (!ReadAndTrimString(battery_path, kBatteryStatusFileName, &status)) {
    status_message_ =
        kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }
  if (status != kBatteryStatusDischargingValue) {
    status_message_ = kBatteryDischargeRoutineNotDischargingMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::Optional<uint32_t> beginning_charge_percent =
      CalculateBatteryChargePercent(root_dir_);
  if (!beginning_charge_percent.has_value()) {
    status_message_ =
        kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  start_ticks_ = tick_clock_->NowTicks();

  callback_.Reset(base::Bind(&BatteryDischargeRoutine::DetermineRoutineResult,
                             weak_ptr_factory_.GetWeakPtr(),
                             beginning_charge_percent.value()));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, callback_.callback(), exec_duration_);

  status_message_ = kBatteryDischargeRoutineRunningMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kRunning;
}

void BatteryDischargeRoutine::DetermineRoutineResult(
    uint32_t beginning_charge_percent) {
  base::Optional<uint32_t> ending_charge_percent =
      CalculateBatteryChargePercent(root_dir_);
  if (!ending_charge_percent.has_value()) {
    status_message_ =
        kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryDischargeRoutineFailedReadingBatteryAttributesMessage;
    return;
  }

  uint32_t ending_charge_percent_value = ending_charge_percent.value();
  if (beginning_charge_percent < ending_charge_percent_value) {
    status_message_ = kBatteryDischargeRoutineNotDischargingMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    LOG(ERROR) << kBatteryDischargeRoutineNotDischargingMessage;
    return;
  }

  uint32_t discharge_percent =
      beginning_charge_percent - ending_charge_percent_value;
  output_ =
      base::StringPrintf("Battery discharged %d%% in %" PRId64 " seconds.",
                         discharge_percent, exec_duration_.InSeconds());
  if (discharge_percent > maximum_discharge_percent_allowed_) {
    status_message_ = kBatteryDischargeRoutineFailedExcessiveDischargeMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return;
  }

  status_message_ = kBatteryDischargeRoutineSucceededMessage;
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
