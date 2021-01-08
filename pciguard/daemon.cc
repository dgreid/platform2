// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pciguard/daemon.h"
#include "pciguard/pciguard_utils.h"

#include <dbus/pciguard/dbus-constants.h>
#include <sysexits.h>

namespace pciguard {

Daemon::Daemon() : DBusServiceDaemon(kPciguardServiceName) {}

int Daemon::OnInit() {
  LOG(INFO) << "pciguard daemon starting...";

  int exit_code = pciguard::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  event_handler_ = std::make_shared<EventHandler>();

  exit_code = DBusServiceDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  // Begin monitoring the session events
  session_monitor_ = std::make_unique<SessionMonitor>(bus_, event_handler_);

  // Begin monitoring the thunderbolt udev events
  tbt_udev_monitor_ = std::make_unique<TbtUdevMonitor>(event_handler_);

  LOG(INFO) << "pciguard daemon started";

  return EX_OK;
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr, bus_, dbus::ObjectPath(kPciguardServicePath));

  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kPciguardServiceInterface);
  CHECK(dbus_interface) << "Couldn't get dbus_interface";

  dbus_interface->AddSimpleMethodHandler(kSetExternalPciDevicesPermissionMethod,
                                         base::Unretained(this),
                                         &Daemon::HandleUserPermissionChanged);
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object", true /* failure_is_fatal */));
}

void Daemon::HandleUserPermissionChanged(bool ext_pci_allowed) {
  DCHECK(event_handler_);
  event_handler_->OnUserPermissionChanged(ext_pci_allowed);
}
}  // namespace pciguard
