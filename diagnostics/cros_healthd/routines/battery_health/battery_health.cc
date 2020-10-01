// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_health/battery_health.h"

#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

const struct {
  const char* battery_log_key;
  const char* relative_file_path;
} kBatteryLogKeyPaths[] = {
    {"Manufacturer", kBatteryManufacturerFileName},
    {"Current Now", kBatteryCurrentNowFileName},
    {"Present", kBatteryPresentFileName},
    {"Status", kBatteryStatusFileName},
    {"Voltage Now", kBatteryVoltageNowFileName},
    {"Charge Full", kBatteryChargeFullFileName},
    {"Charge Full Design", kBatteryChargeFullDesignFileName},
    {"Charge Now", kBatteryChargeNowFileName}};

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

bool ReadBatteryCapacities(const base::FilePath& root_dir,
                           uint32_t* capacity,
                           uint32_t* design_capacity) {
  DCHECK(capacity);
  DCHECK(design_capacity);

  base::FilePath absolute_charge_full_path(
      root_dir.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeFullFileName));
  base::FilePath absolute_charge_full_design_path(
      root_dir.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeFullDesignFileName));
  if (!TryReadFileToUint(absolute_charge_full_path, capacity) ||
      !TryReadFileToUint(absolute_charge_full_design_path, design_capacity)) {
    // No charge values, check for energy-reporting batteries.
    base::FilePath absolute_energy_full_path(
        root_dir.AppendASCII(kBatteryDirectoryPath)
            .AppendASCII(kBatteryEnergyFullFileName));
    base::FilePath absolute_energy_full_design_path(
        root_dir.AppendASCII(kBatteryDirectoryPath)
            .AppendASCII(kBatteryEnergyFullDesignFileName));
    if (!TryReadFileToUint(absolute_energy_full_path, capacity) ||
        !TryReadFileToUint(absolute_energy_full_design_path, design_capacity)) {
      return false;
    }
  }

  return true;
}

bool ReadCycleCount(const base::FilePath& root_dir, uint32_t* cycle_count) {
  DCHECK(cycle_count);

  base::FilePath absolute_cycle_count_path(
      root_dir.AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryCycleCountFileName));
  if (!TryReadFileToUint(absolute_cycle_count_path, cycle_count))
    return false;

  return true;
}

bool TestWearPercentage(const base::FilePath& root_dir,
                        uint32_t percent_battery_wear_allowed,
                        mojo_ipc::DiagnosticRoutineStatusEnum* status,
                        std::string* status_message,
                        std::string* output) {
  uint32_t capacity;
  uint32_t design_capacity;

  if (percent_battery_wear_allowed > 100) {
    *status_message = kBatteryHealthInvalidParametersMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  if (!ReadBatteryCapacities(root_dir, &capacity, &design_capacity) ||
      capacity < 0 || design_capacity < 0) {
    *status_message = kBatteryHealthFailedCalculatingWearPercentageMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  // Cap the wear percentage at 0. There are cases where the capacity can be
  // higher than the design capacity, due to variance in batteries or vendors
  // setting conservative design capacities.
  uint32_t wear_percentage =
      capacity > design_capacity ? 0 : 100 - capacity * 100 / design_capacity;

  *output += "Wear Percentage: " + std::to_string(wear_percentage) + "\n";
  if (wear_percentage > percent_battery_wear_allowed) {
    *status_message = kBatteryHealthExcessiveWearMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

bool TestCycleCount(const base::FilePath& root_dir,
                    uint32_t maximum_cycle_count,
                    mojo_ipc::DiagnosticRoutineStatusEnum* status,
                    std::string* status_message,
                    std::string* output) {
  uint32_t cycle_count;
  if (!ReadCycleCount(root_dir, &cycle_count) || cycle_count < 0) {
    *status_message = kBatteryHealthFailedReadingCycleCountMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    return false;
  }

  *output += "Cycle Count: " + std::to_string(cycle_count) + "\n";
  if (cycle_count > maximum_cycle_count) {
    *status_message = kBatteryHealthExcessiveCycleCountMessage;
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    return false;
  }

  return true;
}

void RunBatteryHealthRoutine(Context* const context,
                             uint32_t maximum_cycle_count,
                             uint32_t percent_battery_wear_allowed,
                             mojo_ipc::DiagnosticRoutineStatusEnum* status,
                             std::string* status_message,
                             std::string* output) {
  DCHECK(context);
  DCHECK(status);
  DCHECK(status_message);
  DCHECK(output);

  const base::FilePath root_dir = context->root_dir();
  for (const auto& item : kBatteryLogKeyPaths) {
    std::string file_contents;
    base::FilePath absolute_file_path(
        root_dir.AppendASCII(kBatteryDirectoryPath)
            .AppendASCII(item.relative_file_path));
    if (!TryReadFileToString(absolute_file_path, &file_contents)) {
      // Failing to read and log a file should not cause the routine to fail,
      // but we should record the event.
      PLOG(WARNING) << "Battery attribute unavailable: "
                    << item.battery_log_key;
      continue;
    }

    *output += base::StringPrintf("%s: %s\n", item.battery_log_key,
                                  file_contents.c_str());
  }

  if (!TestWearPercentage(root_dir, percent_battery_wear_allowed, status,
                          status_message, output) ||
      !TestCycleCount(root_dir, maximum_cycle_count, status, status_message,
                      output)) {
    return;
  }

  *status_message = kBatteryHealthRoutinePassedMessage;
  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace

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

std::unique_ptr<DiagnosticRoutine> CreateBatteryHealthRoutine(
    Context* const context,
    uint32_t maximum_cycle_count,
    uint32_t percent_battery_wear_allowed) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunBatteryHealthRoutine, context, maximum_cycle_count,
                     percent_battery_wear_allowed));
}

}  // namespace diagnostics
