// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/emmc_device_adapter.h"

namespace diagnostics {

TEST(EmmcDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk0";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("mmcblk0", adapter.GetDeviceName());
  EXPECT_EQ("test_mmc_model", adapter.GetModel());
}

// Test when device is present, but data is missing.
TEST(EmmcDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk1";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("mmcblk1", adapter.GetDeviceName());
  EXPECT_EQ("", adapter.GetModel());
}

}  // namespace diagnostics
