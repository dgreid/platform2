// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_
#define BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_

#include <memory>

#include "bluetooth/common/bluetooth_daemon.h"
#include "bluetooth/dispatcher/dispatcher.h"
#include "bluetooth/dispatcher/dispatcher_debug_manager.h"

namespace bluetooth {

// Main class within btdispatch daemon that ties all other classes together.
class DispatcherDaemon : public BluetoothDaemon {
 public:
  // |passthrough_mode|: Pure D-Bus forwarding to/from BlueZ or NewBlue.
  explicit DispatcherDaemon(PassthroughMode passthrough_mode);
  ~DispatcherDaemon() override = default;

  // Initializes the daemon D-Bus operations.
  bool Init(scoped_refptr<dbus::Bus> bus, DBusDaemon* dbus_daemon) override;

 private:
  // The exported object manager to be shared with other components
  std::unique_ptr<ExportedObjectManagerWrapper>
      exported_object_manager_wrapper_;

  // Exposes D-Bus API to enable debug logs
  std::unique_ptr<DispatcherDebugManager> debug_manager_;

  // Exposes BlueZ-compatible D-Bus API and handles the client requests.
  std::unique_ptr<Dispatcher> dispatcher_;

  PassthroughMode passthrough_mode_;

  DISALLOW_COPY_AND_ASSIGN(DispatcherDaemon);
};

}  // namespace bluetooth

#endif  // BLUETOOTH_DISPATCHER_DISPATCHER_DAEMON_H_
