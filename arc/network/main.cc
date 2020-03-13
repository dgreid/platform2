// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "arc/network/adb_proxy.h"
#include "arc/network/helper_process.h"
#include "arc/network/manager.h"
#include "arc/network/multicast_proxy.h"
#include "arc/network/ndproxy.h"
#include "arc/network/socket.h"

int main(int argc, char* argv[]) {
  DEFINE_bool(log_to_stderr, false, "Log to both syslog and stderr");
  DEFINE_int32(
      adb_proxy_fd, -1,
      "Control socket for starting the ADB proxy subprocess. Used internally.");
  DEFINE_int32(mcast_proxy_fd, -1,
               "Control socket for starting the multicast proxy "
               "subprocess. Used internally.");
  DEFINE_int32(
      nd_proxy_fd, -1,
      "Control socket for starting the ND proxy subprocess. Used internally.");

  brillo::FlagHelper::Init(argc, argv, "ARC network daemon");

  int flags = brillo::kLogToSyslog | brillo::kLogHeader;
  if (FLAGS_log_to_stderr)
    flags |= brillo::kLogToStderr;
  brillo::InitLog(flags);

  if (FLAGS_adb_proxy_fd >= 0) {
    LOG(INFO) << "Spawning adb proxy";
    base::ScopedFD fd(FLAGS_adb_proxy_fd);
    arc_networkd::AdbProxy adb_proxy(std::move(fd));
    return adb_proxy.Run();
  }

  if (FLAGS_nd_proxy_fd >= 0) {
    LOG(INFO) << "Spawning nd proxy";
    base::ScopedFD fd(FLAGS_nd_proxy_fd);
    arc_networkd::NDProxyDaemon nd_proxy(std::move(fd));
    return nd_proxy.Run();
  }

  if (FLAGS_mcast_proxy_fd >= 0) {
    LOG(INFO) << "Spawning multicast proxy";
    base::ScopedFD fd(FLAGS_mcast_proxy_fd);
    arc_networkd::MulticastProxy mcast_proxy(std::move(fd));
    return mcast_proxy.Run();
  }

  auto adb_proxy = std::make_unique<arc_networkd::HelperProcess>();
  adb_proxy->Start(argc, argv, "--adb_proxy_fd");

  auto mcast_proxy = std::make_unique<arc_networkd::HelperProcess>();
  mcast_proxy->Start(argc, argv, "--mcast_proxy_fd");

  auto nd_proxy = std::make_unique<arc_networkd::HelperProcess>();
  nd_proxy->Start(argc, argv, "--nd_proxy_fd");

  LOG(INFO) << "Starting arc-networkd manager";
  arc_networkd::Manager manager(std::move(adb_proxy), std::move(mcast_proxy),
                                std::move(nd_proxy));
  return manager.Run();
}
