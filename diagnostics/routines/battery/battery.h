// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_BATTERY_BATTERY_H_
#define DIAGNOSTICS_ROUTINES_BATTERY_BATTERY_H_

#include <cstdint>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/routines/diag_routine.h"

namespace diagnostics {

// Relative path to the charge_full_design file read by the battery routine.
extern const char kBatteryChargeFullDesignPath[];
// Output messages for the battery routine when in various states.
extern const char kBatteryRoutineParametersInvalidMessage[];
extern const char kBatteryFailedReadingChargeFullDesignMessage[];
extern const char kBatteryFailedParsingChargeFullDesignMessage[];
extern const char kBatteryRoutineSucceededMessage[];
extern const char kBatteryRoutineFailedMessage[];

// The battery routine checks whether or not the battery's design capacity is
// within the given limits. It reads the design capacity from the file
// kBatteryChargeFullDesignPath.
class BatteryRoutine final : public DiagnosticRoutine {
 public:
  BatteryRoutine(uint32_t low_mah, uint32_t high_mah);
  BatteryRoutine(const BatteryRoutine&) = delete;
  BatteryRoutine& operator=(const BatteryRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~BatteryRoutine() override;
  void Start() override;
  void Resume() override;
  void Cancel() override;
  void PopulateStatusUpdate(
      chromeos::cros_healthd::mojom::RoutineUpdate* response,
      bool include_output) override;
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum GetStatus()
      override;

  // Overrides the file system root directory for file operations in tests.
  // If used, this function needs to be called before Start().
  void set_root_dir_for_testing(const base::FilePath& root_dir);

 private:
  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum
  RunBatteryRoutine();

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  uint32_t low_mah_;
  uint32_t high_mah_;
  std::string status_message_;
  base::FilePath root_dir_{"/"};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_BATTERY_BATTERY_H_
