// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_MANAGER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_MANAGER_H_

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/synchronization/lock.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/device_info.h"
#include "diagnostics/cros_healthd/utils/storage/device_lister.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

// Manages StorageDeviceInfo structures for present block devices.
class StorageDeviceManager final {
 public:
  StorageDeviceManager(std::unique_ptr<StorageDeviceLister> device_lister,
                       std::unique_ptr<brillo::Udev> udev,
                       std::unique_ptr<Platform> platform);
  StorageDeviceManager(const StorageDeviceManager&) = delete;
  StorageDeviceManager(StorageDeviceManager&&) = delete;
  StorageDeviceManager& operator=(const StorageDeviceManager&) = delete;
  StorageDeviceManager& operator=(StorageDeviceManager&&) = delete;

  StatusOr<std::vector<
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>>
  FetchDevicesInfo(const base::FilePath& root);

 private:
  // Updates the list of present non-removable block devices;
  Status RefreshDevices(const base::FilePath& root);

  // Returns a list of sysfs paths of non-removable block devices;
  std::vector<base::FilePath> ListDevicesPaths(
      const base::FilePath& root) const;

  const std::unique_ptr<const StorageDeviceLister> device_lister_;
  std::unique_ptr<brillo::Udev> udev_;  // Has non-const interface.
  const std::unique_ptr<const Platform> platform_;

  // fetch_lock_ must be held throughout the whole fetch process.
  // It protects the list of devices, which is preserved between calls.
  base::Lock fetch_lock_;
  std::map<base::FilePath, std::unique_ptr<StorageDeviceInfo>> devices_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_MANAGER_H_
