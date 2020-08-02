// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_STORAGE_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_STORAGE_DEVICE_ADAPTER_H_

#include <cstdint>
#include <string>
#include "diagnostics/common/statusor.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// StorageDeviceAdapter is an accessor interface to the subsystem-specific
// information in a uniform way.
class StorageDeviceAdapter {
 public:
  virtual ~StorageDeviceAdapter() = default;

  virtual std::string GetDeviceName() const = 0;
  virtual StatusOr<chromeos::cros_healthd::mojom::BlockDeviceVendor>
  GetVendorId() const = 0;
  virtual StatusOr<chromeos::cros_healthd::mojom::BlockDeviceProduct>
  GetProductId() const = 0;
  virtual StatusOr<chromeos::cros_healthd::mojom::BlockDeviceRevision>
  GetRevision() const = 0;
  virtual StatusOr<std::string> GetModel() const = 0;
  virtual StatusOr<chromeos::cros_healthd::mojom::BlockDeviceFirmware>
  GetFirmwareVersion() const = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_STORAGE_DEVICE_ADAPTER_H_
