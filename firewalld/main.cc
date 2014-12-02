// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <chromeos/syslog_logging.h>

#include "firewalld/firewall_daemon.h"

using firewalld::FirewallDaemon;

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  chromeos::InitLog(chromeos::kLogToSyslog);

  FirewallDaemon daemon;
  return daemon.Run();
}
