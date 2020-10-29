// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_DAEMON_H_
#define TYPECD_DAEMON_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "typecd/port_manager.h"
#include "typecd/udev_monitor.h"

namespace typecd {

class Daemon : public brillo::DBusDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  ~Daemon() override;

 protected:
  int OnInit() override;

 private:
  std::unique_ptr<UdevMonitor> udev_monitor_;
  std::unique_ptr<PortManager> port_manager_;
  base::WeakPtrFactory<Daemon> weak_factory_;
};

}  // namespace typecd

#endif  // TYPECD_DAEMON_H__
