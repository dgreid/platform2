// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/stateful_partition_utils.h"

#include <cstdint>

#include <base/files/file_path.h>
#include <base/system/sys_info.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

constexpr auto kStatefulPartitionPath = "mnt/stateful_partition";

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::StatefulPartitionResultPtr FetchStatefulPartitionInfo(
    const base::FilePath& root_dir) {
  const auto statefulPartitionPath = root_dir.Append(kStatefulPartitionPath);
  const int64_t available_space =
      base::SysInfo::AmountOfFreeDiskSpace(statefulPartitionPath);
  const int64_t total_space =
      base::SysInfo::AmountOfTotalDiskSpace(statefulPartitionPath);

  if (available_space < 0 || total_space < 0) {
    return mojo_ipc::StatefulPartitionResult::NewError(
        CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                               "Failed to collect stateful_partition info"));
  }

  return mojo_ipc::StatefulPartitionResult::NewPartitionInfo(
      mojo_ipc::StatefulPartitionInfo::New(
          static_cast<uint64_t>(available_space),
          static_cast<uint64_t>(total_space)));
}

}  // namespace diagnostics
