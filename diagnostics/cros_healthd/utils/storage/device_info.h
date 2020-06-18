// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_

#include <cstdint>
#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/disk_iostat.h"
#include "diagnostics/cros_healthd/utils/storage/platform.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// StorageDeviceInfo encapsulates the logic for retrieving info about an
// individual storage device.
class StorageDeviceInfo {
 public:
  static std::unique_ptr<StorageDeviceInfo> Create(
      const base::FilePath& dev_sys_path,
      const base::FilePath& dev_node_path,
      const std::string& subsystem,
      std::unique_ptr<Platform> platform = std::make_unique<Platform>());

  // PopulateDeviceInfo fills the fields of Mojo's data structure representing
  // a block device. It is responsible for population of most of the info.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  PopulateDeviceInfo(
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfo* output_info);

  // PopulateLegaceInfo fills the fields of Mojo's data structure representing
  // a block device. It is responsible for population of fields which are kept
  // for compatibility with the existing applications and will be gradually
  // replaced.
  void PopulateLegacyFields(
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfo* output_info);

 private:
  const base::FilePath dev_sys_path_;
  const base::FilePath dev_node_path_;
  const std::string subsystem_;
  const std::unique_ptr<const StorageDeviceAdapter> adapter_;
  const std::unique_ptr<const Platform> platform_;

  DiskIoStat iostat_;

  StorageDeviceInfo(const base::FilePath& dev_sys_path,
                    const base::FilePath& dev_node_path,
                    const std::string& subsystem,
                    std::unique_ptr<StorageDeviceAdapter> adapter,
                    std::unique_ptr<Platform> platform);
  StorageDeviceInfo(const StorageDeviceInfo&) = delete;
  StorageDeviceInfo(StorageDeviceInfo&&) = delete;
  StorageDeviceInfo& operator=(const StorageDeviceInfo&) = delete;
  StorageDeviceInfo& operator=(StorageDeviceInfo&&) = delete;

  // Implementation of the field population. The separation is used for uniform
  // error propagation via Status.
  Status PopulateDeviceInfoImpl(
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfo* output_info);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_
