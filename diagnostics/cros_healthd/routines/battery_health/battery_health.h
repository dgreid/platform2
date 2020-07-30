// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_

#include <cstdint>
#include <map>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/routines/diag_routine.h"

namespace diagnostics {
// Relative path to the directory with files read by the BatteryHealth routine.
extern const char kBatterySysfsPath[];
// Paths relative to |kBatterySysfsPath| to individual files read by the
// BatteryHealth routine.
extern const char kBatteryHealthChargeFullPath[];
extern const char kBatteryHealthChargeFullDesignPath[];
extern const char kBatteryHealthEnergyFullPath[];
extern const char kBatteryHealthEnergyFullDesignPath[];
extern const char kBatteryHealthCycleCountPath[];
extern const char kBatteryHealthManufacturerPath[];
extern const char kBatteryHealthCurrentNowPath[];
extern const char kBatteryHealthPresentPath[];
extern const char kBatteryHealthStatusPath[];
extern const char kBatteryHealthVoltageNowPath[];
extern const char kBatteryHealthChargeNowPath[];

// Status messages for the BatteryHealth routine when in various states.
extern const char kBatteryHealthInvalidParametersMessage[];
extern const char kBatteryHealthFailedCalculatingWearPercentageMessage[];
extern const char kBatteryHealthExcessiveWearMessage[];
extern const char kBatteryHealthFailedReadingCycleCountMessage[];
extern const char kBatteryHealthExcessiveCycleCountMessage[];
extern const char kBatteryHealthRoutinePassedMessage[];

// The battery health routine checks whether or not the battery's design
// capacity is within the given limits. It reads the design capacity from the
// file kBatteryChargeFullDesignPath.
class BatteryHealthRoutine final : public DiagnosticRoutine {
 public:
  BatteryHealthRoutine(uint32_t maximum_cycle_count,
                       uint32_t percent_battery_wear_allowed);
  BatteryHealthRoutine(const BatteryHealthRoutine&) = delete;
  BatteryHealthRoutine& operator=(const BatteryHealthRoutine&) = delete;

  // DiagnosticRoutine overrides:
  ~BatteryHealthRoutine() override;
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
  bool RunBatteryHealthRoutine();
  bool ReadBatteryCapacities(uint32_t* capacity, uint32_t* design_capacity);
  bool ReadCycleCount(uint32_t* cycle_count);
  bool TestWearPercentage();
  bool TestCycleCount();

  chromeos::cros_healthd::mojom::DiagnosticRoutineStatusEnum status_;
  uint32_t maximum_cycle_count_;
  uint32_t percent_battery_wear_allowed_;
  std::map<std::string, std::string> battery_health_log_;
  std::string status_message_;
  base::FilePath root_dir_{"/"};
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BATTERY_HEALTH_BATTERY_HEALTH_H_
