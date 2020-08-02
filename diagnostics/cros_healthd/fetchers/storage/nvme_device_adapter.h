// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_NVME_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_NVME_DEVICE_ADAPTER_H_

#include <string>

#include <base/files/file_path.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/fetchers/storage/storage_device_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// NVME-specific data retrieval module.
class NvmeDeviceAdapter : public StorageDeviceAdapter {
 public:
  explicit NvmeDeviceAdapter(const base::FilePath& dev_sys_path);
  NvmeDeviceAdapter(const NvmeDeviceAdapter&) = delete;
  NvmeDeviceAdapter(NvmeDeviceAdapter&&) = delete;
  NvmeDeviceAdapter& operator=(const NvmeDeviceAdapter&) = delete;
  NvmeDeviceAdapter& operator=(NvmeDeviceAdapter&&) = delete;
  ~NvmeDeviceAdapter() override = default;

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

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_NVME_DEVICE_ADAPTER_H_
