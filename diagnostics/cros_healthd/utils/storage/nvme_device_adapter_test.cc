// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/nvme_device_adapter.h"

namespace diagnostics {

TEST(NvmeDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n1";
  NvmeDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("nvme0n1", adapter.GetDeviceName());
  EXPECT_EQ("test_nvme_model", adapter.GetModel());
}

// Test when device is present, but data is missing.
TEST(NvmeDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n2";
  NvmeDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("nvme0n2", adapter.GetDeviceName());
  EXPECT_EQ("", adapter.GetModel());
}

}  // namespace diagnostics
