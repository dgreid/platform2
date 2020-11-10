// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_COMMON_BLUETOOTH_DAEMON_H_
#define BLUETOOTH_COMMON_BLUETOOTH_DAEMON_H_

#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

namespace bluetooth {

class DBusDaemon;

// The interface of bluetooth::DBusDaemon's delegate.
class BluetoothDaemon {
 public:
  BluetoothDaemon() = default;
  BluetoothDaemon(const BluetoothDaemon&) = delete;
  BluetoothDaemon& operator=(const BluetoothDaemon&) = delete;

  virtual ~BluetoothDaemon() = default;

  // Initializes the daemon's D-Bus operation.
  // Returns true if succeeds, false otherwise.
  // |bus| is the main D-Bus connection provided by DBusDaemon.
  // |dbus_daemon| refers to the delegator (DBusDaemon). Pointer is not owned.
  virtual bool Init(scoped_refptr<dbus::Bus> bus, DBusDaemon* dbus_daemon) = 0;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_COMMON_BLUETOOTH_DAEMON_H_
