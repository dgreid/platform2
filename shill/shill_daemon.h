// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SHILL_DAEMON_H_
#define SHILL_SHILL_DAEMON_H_

#include <base/macros.h>
#include <brillo/daemons/daemon.h>

#include "shill/daemon_task.h"

namespace shill {

class Config;

// ShillDaemon is the daemon that will be initialized in shill_main.cc. It
// delegates the logic of daemon-related tasks (e.g. init/shutdown, start/stop)
// to DaemonTask, and additionally overrides methods of brillo::Daemon.
class ShillDaemon : public brillo::Daemon {
 public:
  ShillDaemon(const shill::DaemonTask::Settings& settings, Config* config);
  virtual ~ShillDaemon();

 private:
  // Implementation of brillo::Daemon.
  int OnInit() override;
  void OnShutdown(int* return_code) override;

  DaemonTask daemon_task_;

  DISALLOW_COPY_AND_ASSIGN(ShillDaemon);
};

}  // namespace shill

#endif  // SHILL_SHILL_DAEMON_H_
