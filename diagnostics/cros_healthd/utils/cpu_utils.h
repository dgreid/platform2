// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_UTILS_H_

#include <vector>

#include <base/files/file_path.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Returns a structure with a list of data fields for each of the device's CPUs
// or the error that occurred fetching the information.
chromeos::cros_healthd::mojom::CpuResultPtr FetchCpuInfo(
    const base::FilePath& root_dir);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_CPU_UTILS_H_
