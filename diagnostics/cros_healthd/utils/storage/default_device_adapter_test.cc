// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"

namespace diagnostics {

TEST(DefaultDeviceAdapterTest, ModelFile) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/model_file_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("model_file_test", adapter.GetDeviceName());
  EXPECT_EQ("test0_model", adapter.GetModel());
}

TEST(DefaultDeviceAdapterTest, NameFile) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/name_file_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("name_file_test", adapter.GetDeviceName());
  EXPECT_EQ("test1_model", adapter.GetModel());
}

// Test when device is present, but data is missing.
TEST(DefaultDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/"
      "missing_model_and_name_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("missing_model_and_name_test", adapter.GetDeviceName());
  EXPECT_EQ("", adapter.GetModel());
}

}  // namespace diagnostics
