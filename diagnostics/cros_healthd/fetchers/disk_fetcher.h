// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd/utils/storage/device_info.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The DiskFetcher class is responsible for gathering disk info reported by
// cros_healthd.
class DiskFetcher {
 public:
  DiskFetcher();
  DiskFetcher(const DiskFetcher&) = delete;
  DiskFetcher& operator=(const DiskFetcher&) = delete;
  ~DiskFetcher();

  // Returns a structure with either the device's non-removable block device
  // info or the error that occurred fetching the information.
  chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
  FetchNonRemovableBlockDevicesInfo(const base::FilePath& root_dir);

 private:
  // Fetches information for a single non-removable block device.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  FetchNonRemovableBlockDeviceInfo(
      const std::unique_ptr<StorageDeviceInfo>& dev_info,
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr*
          output_info);

  // Gets the /dev/... name for |sys_path| output parameter, which should be a
  // /sys/block/... name. This utilizes libudev. Also returns the driver
  // |subsystems| output parameter for use in determining the "type" of the
  // block device. If the call is successful, base::nullopt is returned. If an
  // error occurred, a ProbeError is returned and the output parameters do not
  // contain valid information.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  GatherSysPathRelatedInfo(const base::FilePath& sys_path,
                           base::FilePath* devnode_path,
                           std::string* subsystems);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
