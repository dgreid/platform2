// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_

#include <cstdint>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {

// Relative path to the charge_full_design file read by the battery capacity
// routine.
extern const char kBatteryCapacityChargeFullDesignPath[];
// Output messages for the battery capacity routine when in various states.
extern const char kBatteryCapacityRoutineParametersInvalidMessage[];
extern const char kBatteryCapacityFailedReadingChargeFullDesignMessage[];
extern const char kBatteryCapacityFailedParsingChargeFullDesignMessage[];
extern const char kBatteryCapacityRoutineSucceededMessage[];
extern const char kBatteryCapacityRoutineFailedMessage[];

// The battery capacity routine checks whether or not the battery's design
// capacity is within the given limits. It reads the design capacity from the
// file kBatteryCapacityChargeFullDesignPath.
class BatteryCapacityRoutine final : public DiagnosticRoutine {
 public:
  BatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);
  BatteryCapacityRoutine(const BatteryCapacityRoutine&) = delete;
  BatteryCapacityRoutine& operator=(const BatteryCapacityRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~BatteryCapacityRoutine() override;
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
  RunBatteryCapacityRoutine();

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  uint32_t low_mah_;
  uint32_t high_mah_;
  std::string status_message_;
  base::FilePath root_dir_{"/"};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_CAPACITY_BATTERY_CAPACITY_H_
