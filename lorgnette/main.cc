// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <base/bind.h>
#include <base/logging.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include "lorgnette/daemon.h"

namespace {

const char* kLoggerCommand = "/usr/bin/logger";

}  // namespace

// Always logs to the syslog and logs to stderr if
// we are connected to a tty.
void SetupLogging(const char* daemon_name) {
  brillo::InitLog(
      brillo::kLogToSyslog | brillo::kLogToStderrIfTty | brillo::kLogHeader);

  if (!isatty(STDIN_FILENO)) {
    brillo::ProcessImpl logger;
    logger.AddArg(kLoggerCommand);
    logger.AddArg("--priority");
    logger.AddArg("daemon.err");
    logger.AddArg("--tag");
    logger.AddArg(daemon_name);

    logger.RedirectUsingPipe(STDIN_FILENO, true);
    if (!logger.Start()) {
      LOG(ERROR) << "Failed to start logger child.";
      return;
    }

    // Note that we don't set O_CLOEXEC here. This means that stderr
    // from any child processes will, by default, be logged to syslog.
    if (dup2(logger.GetPipe(STDIN_FILENO), fileno(stderr)) != fileno(stderr)) {
      PLOG(ERROR) << "Failed to redirect stderr to syslog";
      return;
    }

    logger.Release();
  }
}

void OnStartup(const char* daemon_name) {
  SetupLogging(daemon_name);
}

int main(int argc, char** argv) {
  lorgnette::Daemon daemon(base::Bind(&OnStartup, argv[0]));

  daemon.Run();

  return 0;
}
