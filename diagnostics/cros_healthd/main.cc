// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>

#include "diagnostics/cros_healthd/cros_healthd.h"
#include "diagnostics/cros_healthd/executor/executor.h"
#include "diagnostics/cros_healthd/minijail/minijail_configuration.h"

int main(int argc, char** argv) {
  brillo::FlagHelper::Init(
      argc, argv, "cros_healthd - Device telemetry and diagnostics daemon.");

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  // Init the Mojo Embedder API here, since both the executor and cros_healthd
  // use it.
  mojo::core::Init();

  // The root-level parent process will continue on as the executor, and the
  // child will become the sandboxed cros_healthd daemon.
  pid_t pid = fork();

  if (pid == -1)
    LOG(FATAL) << "Failed to fork.";

  if (pid != 0) {
    // Parent process:
    if (getuid() != 0)
      LOG(FATAL) << "Executor must run as root";

    // Run the root-level executor.
    return diagnostics::Executor().Run();
  } else {
    // Sandbox the child process.
    diagnostics::ConfigureAndEnterMinijail();

    // Run the cros_healthd daemon.
    return diagnostics::CrosHealthd().Run();
  }
}
