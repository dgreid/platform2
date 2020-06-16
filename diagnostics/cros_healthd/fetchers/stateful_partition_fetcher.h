// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_

#include <base/files/file_path.h>

#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Returns stateful partition data or the error
// that occurred retrieving the information.
chromeos::cros_healthd::mojom::StatefulPartitionResultPtr
FetchStatefulPartitionInfo(const base::FilePath& root_dir);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STATEFUL_PARTITION_FETCHER_H_
