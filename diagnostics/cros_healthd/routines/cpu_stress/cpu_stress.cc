// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/cpu_stress/cpu_stress.h"

#include <string>
#include <vector>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kCpuRoutineExePath[] = "/usr/bin/stressapptest";

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreateCpuStressRoutine(
    base::TimeDelta exec_duration) {
  uint32_t duration_in_seconds = exec_duration.InSeconds();
  std::vector<std::string> cmd{kCpuRoutineExePath, "-W", "-s",
                               std::to_string(duration_in_seconds)};
  if (duration_in_seconds == 0) {
    // Since the execution duration should not be zero, we should let the
    // routine always failed by adding the flag '--force_error' to the
    // stressapptest.
    cmd.push_back("--force_error");
  }

  return std::make_unique<SubprocRoutine>(base::CommandLine(cmd),
                                          duration_in_seconds);
}

}  // namespace diagnostics
