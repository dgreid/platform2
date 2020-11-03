// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pciguard/daemon.h"
#include "pciguard/pciguard_utils.h"

#include <sysexits.h>

namespace pciguard {

int Daemon::OnInit() {
  LOG(INFO) << "pciguard daemon starting...";

  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  exit_code = pciguard::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  event_handler_ = std::make_shared<EventHandler>();

  // Begin monitoring the session events
  session_monitor_ = std::make_unique<SessionMonitor>(bus_, event_handler_);

  // Begin monitoring the thunderbolt udev events
  tbt_udev_monitor_ = std::make_unique<TbtUdevMonitor>(event_handler_);

  LOG(INFO) << "pciguard daemon started";

  return EX_OK;
}

}  // namespace pciguard
