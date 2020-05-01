// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_FAKE_BLUETOOTH_CLIENT_H_
#define DIAGNOSTICS_COMMON_SYSTEM_FAKE_BLUETOOTH_CLIENT_H_

#include <base/macros.h>
#include <dbus/object_path.h>

#include "diagnostics/common/system/bluetooth_client.h"

namespace diagnostics {

class FakeBluetoothClient : public BluetoothClient {
 public:
  FakeBluetoothClient();
  ~FakeBluetoothClient() override;

  bool HasObserver(Observer* observer) const;

  void EmitAdapterAdded(const dbus::ObjectPath& object_path,
                        const AdapterProperties& properties) const;
  void EmitAdapterRemoved(const dbus::ObjectPath& object_path) const;
  void EmitAdapterPropertyChanged(const dbus::ObjectPath& object_path,
                                  const AdapterProperties& properties) const;
  void EmitDeviceAdded(const dbus::ObjectPath& object_path,
                       const DeviceProperties& properties) const;
  void EmitDeviceRemoved(const dbus::ObjectPath& object_path) const;
  void EmitDevicePropertyChanged(const dbus::ObjectPath& object_path,
                                 const DeviceProperties& properties) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(FakeBluetoothClient);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_FAKE_BLUETOOTH_CLIENT_H_
