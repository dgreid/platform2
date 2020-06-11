// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_DEVICE_ADAPTER_H_

#include <cstdint>
#include <string>

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/utils/storage/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

class MockDeviceAdapter : public StorageDeviceAdapter {
 public:
  MockDeviceAdapter() = default;
  MockDeviceAdapter(const MockDeviceAdapter&) = delete;
  MockDeviceAdapter(MockDeviceAdapter&&) = delete;
  MockDeviceAdapter& operator=(const MockDeviceAdapter&) = delete;
  MockDeviceAdapter& operator=(MockDeviceAdapter&&) = delete;
  ~MockDeviceAdapter() override = default;

  MOCK_METHOD(std::string, GetDeviceName, (), (const override));
  MOCK_METHOD(StatusOr<chromeos::cros_healthd::mojom::BlockDeviceVendor>,
              GetVendorId,
              (),
              (const override));
  MOCK_METHOD(StatusOr<chromeos::cros_healthd::mojom::BlockDeviceProduct>,
              GetProductId,
              (),
              (const override));
  MOCK_METHOD(StatusOr<chromeos::cros_healthd::mojom::BlockDeviceRevision>,
              GetRevision,
              (),
              (const override));
  MOCK_METHOD(StatusOr<std::string>, GetModel, (), (const override));
  MOCK_METHOD(StatusOr<chromeos::cros_healthd::mojom::BlockDeviceFirmware>,
              GetFirmwareVersion,
              (),
              (const override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_MOCK_MOCK_DEVICE_ADAPTER_H_
