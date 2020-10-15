// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/urandom/urandom.h"

#include <string>
#include <vector>

#include <base/command_line.h>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

// Path to the urandom executable.
constexpr char kUrandomExePath[] = "/usr/libexec/diagnostics/urandom";

}  // namespace

const base::TimeDelta kUrandomDefaultLengthSeconds =
    base::TimeDelta::FromSeconds(10);

std::unique_ptr<DiagnosticRoutine> CreateUrandomRoutine(
    const base::Optional<base::TimeDelta>& length_seconds) {
  base::TimeDelta routine_duration =
      length_seconds.value_or(kUrandomDefaultLengthSeconds);
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kUrandomExePath,
          "--time_delta_ms=" +
              std::to_string(routine_duration.InMilliseconds()),
          "--urandom_path=/dev/urandom"}),
      routine_duration.InSeconds());
}

}  // namespace diagnostics
