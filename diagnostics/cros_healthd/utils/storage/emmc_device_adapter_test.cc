// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/emmc_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

TEST(EmmcDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk0";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("mmcblk0", adapter.GetDeviceName());
  ASSERT_TRUE(adapter.GetModel().ok());
  EXPECT_EQ("test_mmc_model", adapter.GetModel().value());
}

// Test when device is present, but data is missing.
TEST(EmmcDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk1";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("mmcblk1", adapter.GetDeviceName());
  auto model_or = adapter.GetModel();
  ASSERT_FALSE(model_or.ok());
  EXPECT_EQ(StatusCode::kUnavailable, model_or.status().code());
}

}  // namespace diagnostics
