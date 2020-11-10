// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_COMMON_DBUS_DAEMON_H_
#define BLUETOOTH_COMMON_DBUS_DAEMON_H_

#include <memory>

#include <base/memory/ref_counted.h>
#include <brillo/daemons/daemon.h>
#include <dbus/bus.h>

#include "bluetooth/common/bluetooth_daemon.h"

namespace bluetooth {

// A brillo::Daemon with D-Bus support.
class DBusDaemon : public brillo::Daemon {
 public:
  // |bluetooth_daemon| is a delegate of this daemon.
  explicit DBusDaemon(std::unique_ptr<BluetoothDaemon> bluetooth_daemon);
  DBusDaemon(const DBusDaemon&) = delete;
  DBusDaemon& operator=(const DBusDaemon&) = delete;

  // brillo::Daemon overrides.
  int OnInit() override;

 private:
  scoped_refptr<dbus::Bus> bus_;

  std::unique_ptr<BluetoothDaemon> bluetooth_daemon_;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_COMMON_DBUS_DAEMON_H_
