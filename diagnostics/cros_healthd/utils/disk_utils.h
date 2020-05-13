// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_

#include <cstdint>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>

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
      const base::FilePath& sys_path,
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr*
          output_info);

  // Gets the /dev/... name for |sys_path| output parameter, which should be a
  // /sys/class/block/... name. This utilizes libudev. Also returns the driver
  // |subsystems| output parameter for use in determining the "type" of the
  // block device. If the call is successful, base::nullopt is returned. If an
  // error occurred, a ProbeError is returned and the output parameters do not
  // contain valid information.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  GatherSysPathRelatedInfo(const base::FilePath& sys_path,
                           base::FilePath* devnode_path,
                           std::string* subsystems);

  // Gets the size of the drive in bytes and the size of the drive's sectors in
  // bytes, given the |dev_path|. If the call is successful, base::nullopt is
  // returned. If an error occurred, a ProbeError is returned and neither
  // |size_in_bytes| nor |sector_size_in_bytes| contain valid information.
  base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
  GetDeviceAndSectorSizesInBytes(const base::FilePath& dev_path,
                                 uint64_t* size_in_bytes,
                                 uint64_t* sector_size_in_bytes);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_
