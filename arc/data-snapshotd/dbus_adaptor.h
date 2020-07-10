// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_
#define ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "dbus_adaptors/org.chromium.ArcDataSnapshotd.h"

namespace arc {
namespace data_snapshotd {

class MojoService;

// Implements the "org.chromium.ArcDataSnapshotdInterface" D-Bus interface
// exposed by the arc-data-snapshotd daemon (see constants for the API methods
// at src/platform/system_api/dbus/arc-data-snapshotd/dbus-constants.h).
class DBusAdaptor final : public org::chromium::ArcDataSnapshotdAdaptor,
                          public org::chromium::ArcDataSnapshotdInterface {
 public:
  DBusAdaptor();
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;
  ~DBusAdaptor() override;

  // Registers the D-Bus object that the arc-data-snapshotd daemon exposes and
  // ties methods exposed by this object with the actual implementation.
  void RegisterAsync(const scoped_refptr<dbus::Bus>& bus,
                     brillo::dbus_utils::AsyncEventSequencer* sequencer);

  // Implementation of the "org.chromium.ArcDataSnapshotdInterface" D-Bus
  // interface:
  bool GenerateKeyPair(brillo::ErrorPtr* error) override;

 private:
  // Manages the D-Bus interfaces exposed by the arc-data-snapshotd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace data_snapshotd
}  // namespace arc

#endif  // ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_
