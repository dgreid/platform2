// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PCIGUARD_DAEMON_H_
#define PCIGUARD_DAEMON_H_

#include "pciguard/session_monitor.h"
#include "pciguard/tbt_udev_monitor.h"

#include <brillo/daemons/dbus_daemon.h>
#include <memory>

namespace pciguard {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon();
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;
  ~Daemon() = default;

 protected:
  int OnInit() override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;
  void HandleUserPermissionChanged(bool ext_pci_allowed);

 private:
  std::shared_ptr<EventHandler> event_handler_;
  std::unique_ptr<SessionMonitor> session_monitor_;
  std::unique_ptr<TbtUdevMonitor> tbt_udev_monitor_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace pciguard

#endif  // PCIGUARD_DAEMON_H__
