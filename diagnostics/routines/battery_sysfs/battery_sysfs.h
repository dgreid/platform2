// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_ROUTINES_BATTERY_SYSFS_BATTERY_SYSFS_H_
#define DIAGNOSTICS_ROUTINES_BATTERY_SYSFS_BATTERY_SYSFS_H_

#include <cstdint>
#include <map>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/routines/diag_routine.h"

namespace diagnostics {
// Relative path to the directory with files read by the BatterySysfs routine.
extern const char kBatterySysfsPath[];
// Paths relative to |kBatterySysfsPath| to individual files read by the
// BatterySysfs routine.
extern const char kBatterySysfsChargeFullPath[];
extern const char kBatterySysfsChargeFullDesignPath[];
extern const char kBatterySysfsEnergyFullPath[];
extern const char kBatterySysfsEnergyFullDesignPath[];
extern const char kBatterySysfsCycleCountPath[];
extern const char kBatterySysfsManufacturerPath[];
extern const char kBatterySysfsCurrentNowPath[];
extern const char kBatterySysfsPresentPath[];
extern const char kBatterySysfsStatusPath[];
extern const char kBatterySysfsVoltageNowPath[];
extern const char kBatterySysfsChargeNowPath[];

// Status messages for the BatterySysfs routine when in various states.
extern const char kBatterySysfsInvalidParametersMessage[];
extern const char kBatterySysfsFailedCalculatingWearPercentageMessage[];
extern const char kBatterySysfsExcessiveWearMessage[];
extern const char kBatterySysfsFailedReadingCycleCountMessage[];
extern const char kBatterySysfsExcessiveCycleCountMessage[];
extern const char kBatterySysfsRoutinePassedMessage[];

// The battery routine checks whether or not the battery's design capacity is
// within the given limits. It reads the design capacity from the file
// kBatteryChargeFullDesignPath.
class BatterySysfsRoutine final : public DiagnosticRoutine {
 public:
  BatterySysfsRoutine(uint32_t maximum_cycle_count,
                      uint32_t percent_battery_wear_allowed);
  BatterySysfsRoutine(const BatterySysfsRoutine&) = delete;
  BatterySysfsRoutine& operator=(const BatterySysfsRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~BatterySysfsRoutine() override;
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
  bool RunBatterySysfsRoutine();
  bool ReadBatteryCapacities(uint32_t* capacity, uint32_t* design_capacity);
  bool ReadCycleCount(uint32_t* cycle_count);
  bool TestWearPercentage();
  bool TestCycleCount();

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  uint32_t maximum_cycle_count_;
  uint32_t percent_battery_wear_allowed_;
  std::map<std::string, std::string> battery_sysfs_log_;
  std::string status_message_;
  base::FilePath root_dir_{"/"};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_ROUTINES_BATTERY_SYSFS_BATTERY_SYSFS_H_
