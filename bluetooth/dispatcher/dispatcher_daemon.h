// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_
#define BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_

#include <memory>

#include "bluetooth/common/bluetooth_daemon.h"
#include "bluetooth/dispatcher/dispatcher_debug_manager.h"

namespace bluetooth {

// Main class within btdispatch daemon that ties all other classes together.
class DispatcherDaemon : public BluetoothDaemon {
 public:
  DispatcherDaemon() = default;
  DispatcherDaemon(const DispatcherDaemon&) = delete;
  DispatcherDaemon& operator=(const DispatcherDaemon&) = delete;

  ~DispatcherDaemon() override = default;

  // Initializes the daemon D-Bus operations.
  bool Init(scoped_refptr<dbus::Bus> bus, DBusDaemon* dbus_daemon) override;

 private:
  // The exported object manager to be shared with other components
  std::unique_ptr<ExportedObjectManagerWrapper>
      exported_object_manager_wrapper_;

  // Exposes D-Bus API to enable debug logs
  std::unique_ptr<DispatcherDebugManager> debug_manager_;
};

}  // namespace bluetooth

#endif  // BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_
