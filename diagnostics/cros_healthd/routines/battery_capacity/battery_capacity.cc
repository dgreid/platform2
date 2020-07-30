// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_capacity/battery_capacity.h"

#include <cstdint>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {
// Conversion factor from uAh to mAh.
constexpr uint32_t kuAhTomAhDivisor = 1000;

uint32_t CalculateProgressPercent(
    mojo_ipc::DiagnosticRoutineStatusEnum status) {
  // Since the battery capacity routine cannot be cancelled, the progress
  // percent can only be 0 or 100.
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed)
    return 100;
  return 0;
}

}  // namespace

const char kBatteryCapacityChargeFullDesignPath[] =
    "sys/class/power_supply/BAT0/charge_full_design";
const char kBatteryCapacityRoutineParametersInvalidMessage[] =
    "Invalid BatteryCapacityRoutineParameters.";
const char kBatteryCapacityFailedReadingChargeFullDesignMessage[] =
    "Failed to read charge_full_design.";
const char kBatteryCapacityFailedParsingChargeFullDesignMessage[] =
    "Failed to parse charge_full_design.";
const char kBatteryCapacityRoutineSucceededMessage[] =
    "Battery design capacity within given limits.";
const char kBatteryCapacityRoutineFailedMessage[] =
    "Battery design capacity not within given limits.";

BatteryCapacityRoutine::BatteryCapacityRoutine(uint32_t low_mah,
                                               uint32_t high_mah)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      low_mah_(low_mah),
      high_mah_(high_mah) {}

BatteryCapacityRoutine::~BatteryCapacityRoutine() = default;

void BatteryCapacityRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  status_ = RunBatteryCapacityRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kPassed)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

// The battery capacity routine can only be started.
void BatteryCapacityRoutine::Resume() {}
void BatteryCapacityRoutine::Cancel() {}

void BatteryCapacityRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  // Because the battery capacity routine is non-interactive, we will never
  // include a user message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = CalculateProgressPercent(status_);
}

mojo_ipc::DiagnosticRoutineStatusEnum BatteryCapacityRoutine::GetStatus() {
  return status_;
}

void BatteryCapacityRoutine::set_root_dir_for_testing(
    const base::FilePath& root_dir) {
  root_dir_ = root_dir;
}

mojo_ipc::DiagnosticRoutineStatusEnum
BatteryCapacityRoutine::RunBatteryCapacityRoutine() {
  if (low_mah_ > high_mah_) {
    status_message_ = kBatteryCapacityRoutineParametersInvalidMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::FilePath charge_full_design_path(
      root_dir_.AppendASCII(kBatteryCapacityChargeFullDesignPath));

  std::string charge_full_design_contents;
  if (!base::ReadFileToString(charge_full_design_path,
                              &charge_full_design_contents)) {
    status_message_ = kBatteryCapacityFailedReadingChargeFullDesignMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  base::TrimWhitespaceASCII(charge_full_design_contents, base::TRIM_TRAILING,
                            &charge_full_design_contents);
  uint32_t charge_full_design_uah;
  if (!base::StringToUint(charge_full_design_contents,
                          &charge_full_design_uah)) {
    status_message_ = kBatteryCapacityFailedParsingChargeFullDesignMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  // Conversion is necessary because the inputs are given in mAh, whereas the
  // design capacity is reported in uAh.
  uint32_t charge_full_design_mah = charge_full_design_uah / kuAhTomAhDivisor;
  if (!(charge_full_design_mah >= low_mah_) ||
      !(charge_full_design_mah <= high_mah_)) {
    status_message_ = kBatteryCapacityRoutineFailedMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
  }

  status_message_ = kBatteryCapacityRoutineSucceededMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
