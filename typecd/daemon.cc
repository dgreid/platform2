// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include "typecd/daemon.h"

namespace typecd {

Daemon::Daemon()
    : udev_monitor_(new UdevMonitor()),
      port_manager_(new PortManager()),
      weak_factory_(this) {}

Daemon::~Daemon() {}

int Daemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  LOG(INFO) << "Daemon started.";
  if (!udev_monitor_->InitUdev()) {
    LOG(ERROR) << "udev init failed.";
    return -1;
  }

  // Add any observers to |udev_monitor_| here.
  udev_monitor_->AddObserver(port_manager_.get());

  udev_monitor_->ScanDevices();
  udev_monitor_->BeginMonitoring();

  return 0;
}

}  // namespace typecd
