// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "diagnostics/common/mojo_utils.h"

namespace diagnostics {
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

uint32_t CalculateProgressPercent(
    mojo_ipc::DiagnosticRoutineStatusEnum status) {
  // Since the battery health routine cannot be cancelled, the progress percent
  // can only be 0 or 100.
  if (status == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
      status == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed)
    return 100;
  return 0;
}

const struct {
  const char* battery_log_key;
  const char* relative_file_path;
} kBatteryLogKeyPaths[] = {
    {"Manufacturer", kBatteryHealthManufacturerPath},
    {"Current Now", kBatteryHealthCurrentNowPath},
    {"Present", kBatteryHealthPresentPath},
    {"Status", kBatteryHealthStatusPath},
    {"Voltage Now", kBatteryHealthVoltageNowPath},
    {"Charge Full", kBatteryHealthChargeFullPath},
    {"Charge Full Design", kBatteryHealthChargeFullDesignPath},
    {"Charge Now", kBatteryHealthChargeNowPath}};

bool TryReadFileToString(const base::FilePath& absolute_file_path,
                         std::string* file_contents) {
  DCHECK(file_contents);

  if (!base::ReadFileToString(absolute_file_path, file_contents))
    return false;

  base::TrimWhitespaceASCII(*file_contents, base::TRIM_TRAILING, file_contents);

  return true;
}

bool TryReadFileToUint(const base::FilePath& absolute_file_path,
                       uint32_t* output) {
  DCHECK(output);

  std::string file_contents;
  if (!base::ReadFileToString(absolute_file_path, &file_contents))
    return false;

  base::TrimWhitespaceASCII(file_contents, base::TRIM_TRAILING, &file_contents);
  if (!base::StringToUint(file_contents, output))
    return false;

  return true;
}

}  // namespace

const char kBatterySysfsPath[] = "sys/class/power_supply/BAT0/";
const char kBatteryHealthChargeFullPath[] = "charge_full";
const char kBatteryHealthChargeFullDesignPath[] = "charge_full_design";
const char kBatteryHealthEnergyFullPath[] = "energy_full";
const char kBatteryHealthEnergyFullDesignPath[] = "energy_full_design";
const char kBatteryHealthCycleCountPath[] = "cycle_count";
const char kBatteryHealthManufacturerPath[] = "manufacturer";
const char kBatteryHealthCurrentNowPath[] = "current_now";
const char kBatteryHealthPresentPath[] = "present";
const char kBatteryHealthStatusPath[] = "status";
const char kBatteryHealthVoltageNowPath[] = "voltage_now";
const char kBatteryHealthChargeNowPath[] = "charge_now";

const char kBatteryHealthInvalidParametersMessage[] =
    "Invalid battery health routine parameters.";
const char kBatteryHealthFailedCalculatingWearPercentageMessage[] =
    "Could not get wear percentage.";
const char kBatteryHealthExcessiveWearMessage[] = "Battery is over-worn.";
const char kBatteryHealthFailedReadingCycleCountMessage[] =
    "Could not get cycle count.";
const char kBatteryHealthExcessiveCycleCountMessage[] =
    "Battery cycle count is too high.";
const char kBatteryHealthRoutinePassedMessage[] = "Routine passed.";

BatteryHealthRoutine::BatteryHealthRoutine(
    uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      maximum_cycle_count_(maximum_cycle_count),
      percent_battery_wear_allowed_(percent_battery_wear_allowed) {}

BatteryHealthRoutine::~BatteryHealthRoutine() = default;

void BatteryHealthRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  if (!RunBatteryHealthRoutine())
    LOG(ERROR) << "Routine failed: " << status_message_;
}

// The battery health routine can only be started.
void BatteryHealthRoutine::Resume() {}
void BatteryHealthRoutine::Cancel() {}

void BatteryHealthRoutine::PopulateStatusUpdate(
    mojo_ipc::RoutineUpdate* response, bool include_output) {
  // Because the battery health routine is non-interactive, we will never
  // include a user message.
  mojo_ipc::NonInteractiveRoutineUpdate update;
  update.status = status_;
  update.status_message = status_message_;

  response->routine_update_union->set_noninteractive_update(update.Clone());
  response->progress_percent = CalculateProgressPercent(status_);

  if (include_output) {
    std::string output;
    for (const auto& key_val : battery_health_log_)
      output += key_val.first + ": " + key_val.second + "\n";
    response->output =
        CreateReadOnlySharedMemoryRegionMojoHandle(base::StringPiece(output));
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum BatteryHealthRoutine::GetStatus() {
  return status_;
}

void BatteryHealthRoutine::set_root_dir_for_testing(
    const base::FilePath& root_dir) {
  root_dir_ = root_dir;
}

bool BatteryHealthRoutine::RunBatteryHealthRoutine() {
  for (const auto& item : kBatteryLogKeyPaths) {
    std::string file_contents;
    base::FilePath absolute_file_path(
        root_dir_.AppendASCII(kBatterySysfsPath)
            .AppendASCII(item.relative_file_path));
    if (!TryReadFileToString(absolute_file_path, &file_contents)) {
      // Failing to read and log a file should not cause the routine to fail,
      // but we should record the event.
      PLOG(WARNING) << "Battery attribute unavailable: "
                    << item.battery_log_key;
      continue;
    }

    battery_health_log_[item.battery_log_key] = file_contents;
  }

  if (!TestWearPercentage() || !TestCycleCount())
    return false;

  status_message_ = kBatteryHealthRoutinePassedMessage;
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
  return true;
}

bool BatteryHealthRoutine::ReadBatteryCapacities(uint32_t* capacity,
                                                 uint32_t* design_capacity) {
  DCHECK(capacity);
  DCHECK(design_capacity);

  base::FilePath absolute_charge_full_path(
      root_dir_.AppendASCII(kBatterySysfsPath)
          .AppendASCII(kBatteryHealthChargeFullPath));
  base::FilePath absolute_charge_full_design_path(
      root_dir_.AppendASCII(kBatterySysfsPath)
          .AppendASCII(kBatteryHealthChargeFullDesignPath));
  if (!TryReadFileToUint(absolute_charge_full_path, capacity) ||
      !TryReadFileToUint(absolute_charge_full_design_path, design_capacity)) {
    // No charge values, check for energy-reporting batteries.
    base::FilePath absolute_energy_full_path(
        root_dir_.AppendASCII(kBatterySysfsPath)
            .AppendASCII(kBatteryHealthEnergyFullPath));
    base::FilePath absolute_energy_full_design_path(
        root_dir_.AppendASCII(kBatterySysfsPath)
            .AppendASCII(kBatteryHealthEnergyFullDesignPath));
    if (!TryReadFileToUint(absolute_energy_full_path, capacity) ||
        !TryReadFileToUint(absolute_energy_full_design_path, design_capacity)) {
      return false;
    }
  }

  return true;
}

bool BatteryHealthRoutine::ReadCycleCount(uint32_t* cycle_count) {
  DCHECK(cycle_count);

  base::FilePath absolute_cycle_count_path(
      root_dir_.AppendASCII(kBatterySysfsPath)
          .AppendASCII(kBatteryHealthCycleCountPath));
  if (!TryReadFileToUint(absolute_cycle_count_path, cycle_count))
    return false;

  return true;
}

bool BatteryHealthRoutine::TestWearPercentage() {
  uint32_t capacity;
  uint32_t design_capacity;

  if (percent_battery_wear_allowed_ > 100) {
    status_message_ = kBatteryHealthInvalidParametersMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  if (!ReadBatteryCapacities(&capacity, &design_capacity) || capacity < 0 ||
      design_capacity < 0) {
    status_message_ = kBatteryHealthFailedCalculatingWearPercentageMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  // Cap the wear percentage at 0. There are cases where the capacity can be
  // higher than the design capacity, due to variance in batteries or vendors
  // setting conservative design capacities.
  uint32_t wear_percentage =
      capacity > design_capacity ? 0 : 100 - capacity * 100 / design_capacity;

  battery_health_log_["Wear Percentage"] = std::to_string(wear_percentage);
  if (wear_percentage > percent_battery_wear_allowed_) {
    status_message_ = kBatteryHealthExcessiveWearMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

bool BatteryHealthRoutine::TestCycleCount() {
  uint32_t cycle_count;
  if (!ReadCycleCount(&cycle_count) || cycle_count < 0) {
    status_message_ = kBatteryHealthFailedReadingCycleCountMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  battery_health_log_["Cycle Count"] = std::to_string(cycle_count);
  if (cycle_count > maximum_cycle_count_) {
    status_message_ = kBatteryHealthExcessiveCycleCountMessage;
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

}  // namespace diagnostics
