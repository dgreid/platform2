// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "arc-networkd/manager.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "Log to both syslog and stderr");
  DEFINE_string(internal_interface, "br0",
                "Name of the host interface that connects to the guest");
  DEFINE_string(container_interface, "arc0",
                "Name of the guest interface that connects to the host");
  DEFINE_int32(con_netns, 0, "Container's network namespace (PID)");

  brillo::FlagHelper::Init(argc, argv, "ARC network daemon");

  int flags = brillo::kLogToSyslog | brillo::kLogHeader;
  if (FLAGS_log_to_stderr)
    flags |= brillo::kLogToStderr;
  brillo::InitLog(flags);

  arc_networkd::Manager::Options opt;
  opt.int_ifname = FLAGS_internal_interface;
  opt.con_ifname = FLAGS_container_interface;
  opt.con_netns = FLAGS_con_netns;

  arc_networkd::Manager manager{opt};
  return manager.Run();
}
