// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/stateful_partition_utils.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

TEST(StatefulPartitionUtils, Success) {
  const auto result = FetchStatefulPartitionInfo(base::FilePath("/"));
  ASSERT_TRUE(result->is_partition_info());
  EXPECT_GE(result->get_partition_info()->available_space, 0);
}

TEST(StatefulPartitionUtils, Failure) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const auto result = FetchStatefulPartitionInfo(temp_dir.GetPath());
  EXPECT_TRUE(result->is_error());
}

}  // namespace diagnostics
