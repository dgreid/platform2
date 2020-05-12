// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_SYSTEM_MOCK_BLUETOOTH_CLIENT_H_
#define DIAGNOSTICS_COMMON_SYSTEM_MOCK_BLUETOOTH_CLIENT_H_

#include <vector>

#include <dbus/object_path.h>
#include <gmock/gmock.h>

#include "diagnostics/common/system/bluetooth_client.h"

namespace diagnostics {

class MockBluetoothClient : public BluetoothClient {
 public:
  MockBluetoothClient();
  MockBluetoothClient(const MockBluetoothClient&) = delete;
  MockBluetoothClient& operator=(const MockBluetoothClient&) = delete;
  ~MockBluetoothClient() override;

  MOCK_METHOD(std::vector<dbus::ObjectPath>, GetAdapters, (), (override));
  MOCK_METHOD(std::vector<dbus::ObjectPath>, GetDevices, (), (override));
  MOCK_METHOD(const BluetoothClient::AdapterProperties*,
              GetAdapterProperties,
              (const dbus::ObjectPath&),
              (override));
  MOCK_METHOD(const BluetoothClient::DeviceProperties*,
              GetDeviceProperties,
              (const dbus::ObjectPath&),
              (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_SYSTEM_MOCK_BLUETOOTH_CLIENT_H_
