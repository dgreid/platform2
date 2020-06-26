// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <iostream>
#include <string>

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd/cros_healthd.h"
#include "diagnostics/cros_healthd/minijail/minijail_configuration.h"

int main(int argc, char** argv) {
  brillo::FlagHelper::Init(
      argc, argv, "cros_healthd - Device telemetry and diagnostics daemon.");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Sandbox the process.
  diagnostics::ConfigureAndEnterMinijail();

  // Run the cros_healthd daemon.
  return diagnostics::CrosHealthd().Run();
}
