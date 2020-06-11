// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_EMMC_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_EMMC_DEVICE_ADAPTER_H_

#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/utils/storage/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// EMMC-specific data retrieval module.
class EmmcDeviceAdapter : public StorageDeviceAdapter {
 public:
  explicit EmmcDeviceAdapter(const base::FilePath& dev_sys_path);
  EmmcDeviceAdapter(const EmmcDeviceAdapter&) = delete;
  EmmcDeviceAdapter(EmmcDeviceAdapter&&) = delete;
  EmmcDeviceAdapter& operator=(const EmmcDeviceAdapter&) = delete;
  EmmcDeviceAdapter& operator=(EmmcDeviceAdapter&&) = delete;
  ~EmmcDeviceAdapter() override = default;

  std::string GetDeviceName() const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceVendor> GetVendorId()
      const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceProduct> GetProductId()
      const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceRevision> GetRevision()
      const override;
  StatusOr<std::string> GetModel() const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceFirmware>
  GetFirmwareVersion() const override;

 private:
  const base::FilePath dev_sys_path_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_EMMC_DEVICE_ADAPTER_H_
