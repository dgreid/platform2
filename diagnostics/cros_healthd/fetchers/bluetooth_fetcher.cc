// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/bluetooth_fetcher.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <dbus/object_path.h>

#include "diagnostics/common/system/bluetooth_client.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

BluetoothFetcher::BluetoothFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

BluetoothFetcher::~BluetoothFetcher() = default;

mojo_ipc::BluetoothResultPtr BluetoothFetcher::FetchBluetoothInfo() {
  BluetoothClient* bluetooth_client = context_->bluetooth_client();
  DCHECK(bluetooth_client);

  std::map<dbus::ObjectPath, uint32_t> num_connected_devices;
  std::vector<dbus::ObjectPath> devices = bluetooth_client->GetDevices();
  for (const auto& device : devices) {
    const BluetoothClient::DeviceProperties* device_properties =
        bluetooth_client->GetDeviceProperties(device);
    if (!device_properties || !device_properties->connected.value())
      continue;

    const dbus::ObjectPath& adapter_path = device_properties->adapter.value();
    num_connected_devices[adapter_path]++;
  }

  std::vector<mojo_ipc::BluetoothAdapterInfoPtr> adapter_info;
  std::vector<dbus::ObjectPath> adapters = bluetooth_client->GetAdapters();
  for (const auto& adapter : adapters) {
    const BluetoothClient::AdapterProperties* adapter_properties =
        bluetooth_client->GetAdapterProperties(adapter);
    if (!adapter_properties)
      continue;

    mojo_ipc::BluetoothAdapterInfo info;
    info.name = adapter_properties->name.value();
    info.address = adapter_properties->address.value();
    info.powered = adapter_properties->powered.value();
    const auto it = num_connected_devices.find(adapter);
    if (it != num_connected_devices.end())
      info.num_connected_devices = it->second;

    adapter_info.push_back(info.Clone());
  }

  return mojo_ipc::BluetoothResult::NewBluetoothAdapterInfo(
      std::move(adapter_info));
}

}  // namespace diagnostics
