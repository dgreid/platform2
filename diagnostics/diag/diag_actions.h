// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_
#define DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_

#include <cstdint>
#include <string>

#include <base/macros.h>
#include <base/time/time.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"

namespace diagnostics {

// This class is responsible for providing the actions corresponding to the
// command-line arguments for the diag tool. Only capable of running a single
// routine at a time.
class DiagActions final {
 public:
  DiagActions();
  ~DiagActions();

  // Print a list of routines available on the platform. Returns true iff all
  // available routines were successfully converted to human-readable strings
  // and printed.
  bool ActionGetRoutines();
  // Run a particular diagnostic routine. See mojo/cros_healthd.mojom for
  // details on the individual routines. Returns true iff the routine completed.
  // Note that this does not mean the routine succeeded, only that it started,
  // ran, and was removed.
  bool ActionRunAcPowerRoutine(bool is_connected,
                               const std::string& power_type);
  bool ActionRunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);
  bool ActionRunBatteryHealthRoutine(uint32_t maximum_cycle_count,
                                     uint32_t percent_battery_wear_allowed);
  bool ActionRunCpuCacheRoutine(const base::TimeDelta& exec_duration);
  bool ActionRunCpuStressRoutine(const base::TimeDelta& exec_duration);
  bool ActionRunFloatingPointAccuracyRoutine(
      const base::TimeDelta& exec_duration);
  bool ActionRunNvmeSelfTestRoutine(bool is_long);
  bool ActionRunNvmeWearLevelRoutine(uint32_t wear_level_threshold);
  bool ActionRunSmartctlCheckRoutine();
  bool ActionRunUrandomRoutine(uint32_t length_seconds);

 private:
  // Helper function to determine when a routine has finished. Also removes the
  // routine corresponding to |id_|.
  bool RunRoutineAndProcessResult();

  // Used to send mojo requests to cros_healthd.
  CrosHealthdMojoAdapter adapter_;
  // ID of the routine being run.
  int32_t id_ = chromeos::cros_healthd::mojom::kFailedToStartId;

  DISALLOW_COPY_AND_ASSIGN(DiagActions);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_DIAG_DIAG_ACTIONS_H_
