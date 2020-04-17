// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/dispatcher_daemon.h"

#include <utility>

#include <base/logging.h>

namespace bluetooth {

DispatcherDaemon::DispatcherDaemon(PassthroughMode passthrough_mode)
    : passthrough_mode_(passthrough_mode) {}

bool DispatcherDaemon::Init(scoped_refptr<dbus::Bus> bus,
                            DBusDaemon* dbus_daemon) {
  LOG(INFO) << "Bluetooth daemon started with passthrough mode = "
            << static_cast<int>(passthrough_mode_);

  auto exported_object_manager =
      std::make_unique<brillo::dbus_utils::ExportedObjectManager>(
          bus,
          dbus::ObjectPath(
              bluetooth_object_manager::kBluetoothObjectManagerServicePath));

  exported_object_manager_wrapper_ =
      std::make_unique<ExportedObjectManagerWrapper>(
          bus, std::move(exported_object_manager));

  if (!bus->RequestOwnershipAndBlock(
          bluetooth_object_manager::kBluetoothObjectManagerServiceName,
          dbus::Bus::REQUIRE_PRIMARY)) {
    LOG(ERROR) << "Failed to acquire D-Bus name ownership";
    return false;
  }

  debug_manager_ = std::make_unique<DispatcherDebugManager>(
      bus, exported_object_manager_wrapper_.get());

  debug_manager_->Init();

  return true;
}

}  // namespace bluetooth
