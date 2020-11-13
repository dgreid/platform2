// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/prime_search/prime_search.h"

#include <string>
#include <vector>

#include <base/command_line.h>

#include "diagnostics/cros_healthd/routines/shared_defaults.h"
#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kPrimeSearchExePath[] = "/usr/libexec/diagnostics/prime-search";

}  // namespace

const uint64_t kPrimeSearchDefaultMaxNum = 1000000;

std::unique_ptr<DiagnosticRoutine> CreatePrimeSearchRoutine(
    const base::Optional<base::TimeDelta>& exec_duration,
    const base::Optional<uint64_t>& max_num) {
  uint32_t duration_in_seconds =
      exec_duration.value_or(kDefaultCpuStressRuntime).InSeconds();
  return std::make_unique<SubprocRoutine>(
      base::CommandLine(std::vector<std::string>{
          kPrimeSearchExePath, "--time=" + std::to_string(duration_in_seconds),
          "--max_num=" +
              std::to_string(max_num.value_or(kPrimeSearchDefaultMaxNum))}),
      duration_in_seconds);
}  // namespace diagnostics

}  // namespace diagnostics
