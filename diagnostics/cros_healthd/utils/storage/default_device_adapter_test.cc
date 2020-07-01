// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

TEST(DefaultDeviceAdapterTest, ModelFile) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/model_file_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("model_file_test", adapter.GetDeviceName());
  ASSERT_TRUE(adapter.GetModel().ok());
  EXPECT_EQ("test0_model", adapter.GetModel().value());
}

TEST(DefaultDeviceAdapterTest, NameFile) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/name_file_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("name_file_test", adapter.GetDeviceName());
  ASSERT_TRUE(adapter.GetModel().ok());
  EXPECT_EQ("test1_model", adapter.GetModel().value());
}

// Test when device is present, but data is missing.
TEST(DefaultDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/"
      "missing_model_and_name_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("missing_model_and_name_test", adapter.GetDeviceName());
  auto model_or = adapter.GetModel();
  ASSERT_FALSE(model_or.ok());
  EXPECT_EQ(StatusCode::kUnavailable, model_or.status().code());
}

}  // namespace diagnostics
