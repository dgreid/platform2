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

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_other());
  ASSERT_TRUE(adapter.GetProductId().value().is_other());
  ASSERT_TRUE(adapter.GetRevision().value().is_other());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_other());

  EXPECT_EQ("model_file_test", adapter.GetDeviceName());
  EXPECT_EQ(0, adapter.GetVendorId().value().get_other());
  EXPECT_EQ(0, adapter.GetProductId().value().get_other());
  EXPECT_EQ(0, adapter.GetRevision().value().get_other());
  EXPECT_EQ("test0_model", adapter.GetModel().value());
  EXPECT_EQ(0, adapter.GetFirmwareVersion().value().get_other());
}

TEST(DefaultDeviceAdapterTest, NameFile) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/name_file_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_other());
  ASSERT_TRUE(adapter.GetProductId().value().is_other());
  ASSERT_TRUE(adapter.GetRevision().value().is_other());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_other());

  EXPECT_EQ("name_file_test", adapter.GetDeviceName());
  EXPECT_EQ(0, adapter.GetVendorId().value().get_other());
  EXPECT_EQ(0, adapter.GetProductId().value().get_other());
  EXPECT_EQ(0, adapter.GetRevision().value().get_other());
  EXPECT_EQ("test1_model", adapter.GetModel().value());
  EXPECT_EQ(0, adapter.GetFirmwareVersion().value().get_other());
}

// Test when device is present, but data is missing.
TEST(DefaultDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/"
      "missing_model_and_name_test";
  DefaultDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_FALSE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_other());
  ASSERT_TRUE(adapter.GetProductId().value().is_other());
  ASSERT_TRUE(adapter.GetRevision().value().is_other());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_other());

  EXPECT_EQ("missing_model_and_name_test", adapter.GetDeviceName());
  EXPECT_EQ(0, adapter.GetVendorId().value().get_other());
  EXPECT_EQ(0, adapter.GetProductId().value().get_other());
  EXPECT_EQ(0, adapter.GetRevision().value().get_other());
  EXPECT_EQ(StatusCode::kUnavailable, adapter.GetModel().status().code());
  EXPECT_EQ(0, adapter.GetFirmwareVersion().value().get_other());
}

}  // namespace diagnostics
