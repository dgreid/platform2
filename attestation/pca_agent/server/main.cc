// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <base/command_line.h>
#include <brillo/syslog_logging.h>

#include "attestation/pca_agent/server/pca_agent_daemon.h"

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch("log_to_stderr")) {
    flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(flags);

  PLOG_IF(FATAL, ::daemon(0, 0) == -1) << "Failed to daemonize";

  return attestation::pca_agent::PcaAgentDaemon().Run();
}
