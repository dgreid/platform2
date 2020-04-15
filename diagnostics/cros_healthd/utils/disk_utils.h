// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_

#include <vector>

#include <base/files/file_path.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Returns a structure with either the device's non-removeable block device info
// or the error that occurred fetching the information.
chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
FetchNonRemovableBlockDevicesInfo(const base::FilePath& root_dir);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_DISK_UTILS_H_
