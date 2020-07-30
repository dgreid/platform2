// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/prime_search/prime_search.h"

#include <string>
#include <vector>

#include <base/command_line.h>

#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kPrimeSearchExePath[] = "/usr/libexec/diagnostics/prime-search";

}  // namespace

std::unique_ptr<DiagnosticRoutine> CreatePrimeSearchRoutine(
    base::TimeDelta exec_duration, uint64_t max_num) {
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kPrimeSearchExePath,
          "--time=" + std::to_string(exec_duration.InSeconds()),
          "--max_num=" + std::to_string(max_num)}),
      exec_duration.InSeconds());
}

}  // namespace diagnostics
