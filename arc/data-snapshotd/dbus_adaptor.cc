// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/data-snapshotd/dbus_adaptor.h"

#include <string>
#include <utility>

#include <base/logging.h>

namespace arc {
namespace data_snapshotd {

DBusAdaptor::DBusAdaptor() : org::chromium::ArcDataSnapshotdAdaptor(this) {}

DBusAdaptor::~DBusAdaptor() = default;

void DBusAdaptor::RegisterAsync(
    const scoped_refptr<dbus::Bus>& bus,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus, GetObjectPath()),
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

bool DBusAdaptor::GenerateKeyPair(brillo::ErrorPtr* error) {
  LOG(WARNING) << "Unimplimented";
  // TODO(b/160387490): Implement method:
  // * Generate a key pair.
  // * Store public key ib BootlockBox.
  // * Show a spinner screen.
  return false;
}

}  // namespace data_snapshotd
}  // namespace arc
