// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_BLUETOOTH_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_BLUETOOTH_UTILS_H_

#include "diagnostics/common/system/bluetooth_client.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The BluetoothFetcher class is responsible for gathering a device's Bluetooth
// information.
class BluetoothFetcher {
 public:
  explicit BluetoothFetcher(BluetoothClient* bluetooth_client);
  BluetoothFetcher(const BluetoothFetcher&) = delete;
  BluetoothFetcher& operator=(const BluetoothFetcher&) = delete;
  ~BluetoothFetcher();

  // Returns the device's Bluetooth information.
  chromeos::cros_healthd::mojom::BluetoothResultPtr FetchBluetoothInfo();

 private:
  // Unowned pointer that outlives this BluetoothFetcher instance.
  BluetoothClient* const bluetooth_client_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_BLUETOOTH_UTILS_H_
