// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/proxy/dbus_service.h"

int main(int argc, char** argv) {
#if USE_CRYPTOHOME_USERDATAAUTH_INTERFACE
  // Initialize command line configuration early, as logging will require
  // command line to be initialized
  base::CommandLine::Init(argc, argv);

  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  int flags = brillo::kLogToSyslog;
  if (cl->HasSwitch("log_to_stderr")) {
    flags |= brillo::kLogToStderr;
  }

  brillo::InitLog(flags);

  cryptohome::CryptohomeProxyDaemon proxy_daemon;
  return proxy_daemon.Run();
#else
  LOG(FATAL) << "cryptohome_userdataauth_interface USE flag is unset, "
                "cryptohome-proxy is disabled.";
  return 0;
#endif
}
