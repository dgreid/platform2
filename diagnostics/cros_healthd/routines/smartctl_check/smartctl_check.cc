// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check.h"

#include <utility>

#include <base/logging.h>
#include <base/process/process_handle.h>
#include <base/command_line.h>

#include "diagnostics/cros_healthd/routines/diag_process_adapter_impl.h"
#include "diagnostics/cros_healthd/routines/subproc_routine.h"

namespace diagnostics {

namespace {

constexpr char kSmartctlCheckExecutableInstallLocation[] =
    "/usr/libexec/diagnostics/smartctl-check";
}

std::unique_ptr<DiagnosticRoutine> CreateSmartctlCheckRoutine() {
  return std::make_unique<SubprocRoutine>(
      base::CommandLine({kSmartctlCheckExecutableInstallLocation}),
      0 /*predicted_duration_in_seconds*/);
}

}  // namespace diagnostics
