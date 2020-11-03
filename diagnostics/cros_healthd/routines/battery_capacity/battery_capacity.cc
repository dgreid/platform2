// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/battery_capacity/battery_capacity.h"

#include <cstdint>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/cros_healthd/utils/battery_utils.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Conversion factor from uAh to mAh.
constexpr uint32_t kuAhTomAhDivisor = 1000;

// We include |output_dict| here to satisfy SimpleRoutine - the battery capacity
// routine never includes an output.
void RunBatteryCapacityRoutine(Context* const context,
                               uint32_t low_mah,
                               uint32_t high_mah,
                               mojo_ipc::DiagnosticRoutineStatusEnum* status,
                               std::string* status_message,
                               base::Value* output_dict) {
  DCHECK(context);
  DCHECK(status);
  DCHECK(status_message);

  if (low_mah > high_mah) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    *status_message = kBatteryCapacityRoutineParametersInvalidMessage;
    return;
  }

  auto charge_full_design_path =
      context->root_dir()
          .AppendASCII(kBatteryDirectoryPath)
          .AppendASCII(kBatteryChargeFullDesignFileName);

  std::string charge_full_design_contents;
  if (!base::ReadFileToString(charge_full_design_path,
                              &charge_full_design_contents)) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    *status_message = kBatteryCapacityFailedReadingChargeFullDesignMessage;
    return;
  }

  base::TrimWhitespaceASCII(charge_full_design_contents, base::TRIM_TRAILING,
                            &charge_full_design_contents);
  uint32_t charge_full_design_uah;
  if (!base::StringToUint(charge_full_design_contents,
                          &charge_full_design_uah)) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kError;
    *status_message = kBatteryCapacityFailedParsingChargeFullDesignMessage;
    return;
  }

  // Conversion is necessary because the inputs are given in mAh, whereas the
  // design capacity is reported in uAh.
  uint32_t charge_full_design_mah = charge_full_design_uah / kuAhTomAhDivisor;
  if (!(charge_full_design_mah >= low_mah) ||
      !(charge_full_design_mah <= high_mah)) {
    *status = mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
    *status_message = kBatteryCapacityRoutineFailedMessage;
    return;
  }

  *status = mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
  *status_message = kBatteryCapacityRoutineSucceededMessage;
  return;
}

}  // namespace

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

const uint32_t kBatteryCapacityDefaultLowMah = 1000;
const uint32_t kBatteryCapacityDefaultHighMah = 10000;

std::unique_ptr<DiagnosticRoutine> CreateBatteryCapacityRoutine(
    Context* const context,
    const base::Optional<uint32_t>& low_mah,
    const base::Optional<uint32_t>& high_mah) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunBatteryCapacityRoutine, context,
                     low_mah.value_or(kBatteryCapacityDefaultLowMah),
                     high_mah.value_or(kBatteryCapacityDefaultHighMah)));
}

}  // namespace diagnostics
