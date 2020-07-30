// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/ac_power/ac_power.h"

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace diagnostics {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

namespace {

// Path to the power_supply directory. All subdirectories will be searched to
// try and find the path to a connected AC adapter.
constexpr char kPowerSupplyDirectoryPath[] = "sys/class/power_supply";
// Names of the two files read by the AC power routine.
constexpr char kOnlineFileName[] = "online";
constexpr char kTypeFileName[] = "type";

// POD struct which holds the whitespace-trimmed contents of the online and type
// files for the power supply under test.
struct PowerSupplyFileContents {
  std::string online;  // Whitespace-trimmed contents of the online file.
  std::string type;    // Whitespace-trimmed contents of the type file.
};

}  // namespace

const char kAcPowerRoutineSucceededMessage[] = "AC Power routine passed.";
const char kAcPowerRoutineFailedNotOnlineMessage[] =
    "Expected online power supply, found offline power supply.";
const char kAcPowerRoutineFailedNotOfflineMessage[] =
    "Expected offline power supply, found online power supply.";
const char kAcPowerRoutineFailedMismatchedPowerTypesMessage[] =
    "Read power type different from expected power type.";
const char kAcPowerRoutineNoValidPowerSupplyMessage[] =
    "No valid AC power supply found.";
const char kAcPowerRoutineCancelledMessage[] = "AC Power routine cancelled.";

// We want a value here that is greater than zero to show that the routine has
// started. But it hasn't really done any work, so the value shouldn't be too
// high.
const uint32_t kAcPowerRoutineWaitingProgressPercent = 33;

AcPowerRoutine::AcPowerRoutine(
    chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
    const base::Optional<std::string>& expected_power_type,
    const base::FilePath& root_dir)
    : status_(mojo_ipc::DiagnosticRoutineStatusEnum::kReady),
      expected_power_status_(expected_status),
      expected_power_type_(expected_power_type),
      root_dir_(root_dir) {}

AcPowerRoutine::~AcPowerRoutine() = default;

void AcPowerRoutine::Start() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kReady);
  // Transition to waiting so the user can plug or unplug the AC adapter as
  // necessary.
  status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting;
  CalculateProgressPercent();
}

void AcPowerRoutine::Resume() {
  DCHECK_EQ(status_, mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting);
  status_ = RunAcPowerRoutine();
  if (status_ != mojo_ipc::DiagnosticRoutineStatusEnum::kPassed)
    LOG(ERROR) << "Routine failed: " << status_message_;
}

void AcPowerRoutine::Cancel() {
  // Only cancel the routine if it's in the waiting state. Otherwise, it either
  // hasn't begun or has already finished.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    status_ = mojo_ipc::DiagnosticRoutineStatusEnum::kCancelled;
    status_message_ = kAcPowerRoutineCancelledMessage;
  }
}

void AcPowerRoutine::PopulateStatusUpdate(mojo_ipc::RoutineUpdate* response,
                                          bool include_output) {
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    mojo_ipc::InteractiveRoutineUpdate interactive_update;
    interactive_update.user_message =
        (expected_power_status_ == mojo_ipc::AcPowerStatusEnum::kConnected)
            ? mojo_ipc::DiagnosticRoutineUserMessageEnum::kPlugInACPower
            : mojo_ipc::DiagnosticRoutineUserMessageEnum::kUnplugACPower;
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
}

mojo_ipc::DiagnosticRoutineStatusEnum AcPowerRoutine::GetStatus() {
  return status_;
}

void AcPowerRoutine::CalculateProgressPercent() {
  // If the routine has been started and is waiting, assign a reasonable
  // progress percentage that signifies the routine has been started.
  if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kWaiting) {
    progress_percent_ = kAcPowerRoutineWaitingProgressPercent;
  } else if (status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kPassed ||
             status_ == mojo_ipc::DiagnosticRoutineStatusEnum::kFailed) {
    // The routine has finished, so report 100.
    progress_percent_ = 100;
  }
}

mojo_ipc::DiagnosticRoutineStatusEnum AcPowerRoutine::RunAcPowerRoutine() {
  base::FileEnumerator dir_enumerator(
      root_dir_.AppendASCII(kPowerSupplyDirectoryPath),
      false /* is_recursive */,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES);

  PowerSupplyFileContents contents;
  bool valid_path_found = false;
  for (base::FilePath path = dir_enumerator.Next(); !path.empty();
       path = dir_enumerator.Next()) {
    // Skip all power supplies of unknown type.
    std::string type;
    if (!base::ReadFileToString(path.AppendASCII(kTypeFileName), &type)) {
      continue;
    }

    // Skip all batteries.
    base::TrimWhitespaceASCII(type, base::TRIM_ALL, &type);
    if (type == "Battery")
      continue;

    // Skip all power supplies which don't populate the online file.
    std::string online;
    if (!base::ReadFileToString(path.AppendASCII(kOnlineFileName), &online))
      continue;

    // If we found an online power supply, then that's the power supply we wish
    // to test.
    base::TrimWhitespaceASCII(online, base::TRIM_ALL, &online);
    if (online == "1") {
      valid_path_found = true;
      contents.online = online;
      contents.type = type;
      break;
    }

    // If we have an offline power supply, but haven't found any online power
    // supplies, then we have a candidate for power supply to test.
    if (!valid_path_found) {
      valid_path_found = true;
      contents.online = online;
      contents.type = type;
    }
  }

  if (!valid_path_found) {
    status_message_ = kAcPowerRoutineNoValidPowerSupplyMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kError;
  }

  // Test the contents of the path's online file against the input value.
  if (expected_power_status_ == mojo_ipc::AcPowerStatusEnum::kConnected &&
      contents.online != "1") {
    status_message_ = kAcPowerRoutineFailedNotOnlineMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
  } else if (expected_power_status_ ==
                 mojo_ipc::AcPowerStatusEnum::kDisconnected &&
             contents.online != "0") {
    status_message_ = kAcPowerRoutineFailedNotOfflineMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
  }

  // Test the contents of the path's type file against the input value. This is
  // an optional test, and won't be performed if |expected_power_type_| wasn't
  // specified.
  if (expected_power_type_.has_value() &&
      expected_power_type_.value() != contents.type) {
    status_message_ = kAcPowerRoutineFailedMismatchedPowerTypesMessage;
    return mojo_ipc::DiagnosticRoutineStatusEnum::kFailed;
  }

  status_message_ = kAcPowerRoutineSucceededMessage;
  return mojo_ipc::DiagnosticRoutineStatusEnum::kPassed;
}

}  // namespace diagnostics
