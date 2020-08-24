// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/dispatcher_daemon.h"

#include <utility>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>

namespace bluetooth {

bool DispatcherDaemon::Init(scoped_refptr<dbus::Bus> bus,
                            DBusDaemon* dbus_daemon) {
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
