// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/nvme_device_adapter.h"

namespace diagnostics {

TEST(NvmeDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n1";
  NvmeDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_nvme_subsystem_vendor());
  ASSERT_TRUE(adapter.GetProductId().value().is_nvme_subsystem_device());
  ASSERT_TRUE(adapter.GetRevision().value().is_nvme_pcie_rev());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_nvme_firmware_rev());

  EXPECT_EQ("nvme0n1", adapter.GetDeviceName());
  EXPECT_EQ(0x1812, adapter.GetVendorId().value().get_nvme_subsystem_vendor());
  EXPECT_EQ(0x3243, adapter.GetProductId().value().get_nvme_subsystem_device());
  EXPECT_EQ(0x13, adapter.GetRevision().value().get_nvme_pcie_rev());
  EXPECT_EQ("test_nvme_model", adapter.GetModel().value());
  EXPECT_EQ(0x5645525F54534554,
            adapter.GetFirmwareVersion().value().get_nvme_firmware_rev());
}

// Test when device is present, but data is missing.
TEST(NvmeDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n2";
  NvmeDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("nvme0n2", adapter.GetDeviceName());

  ASSERT_FALSE(adapter.GetVendorId().ok());
  ASSERT_FALSE(adapter.GetProductId().ok());
  ASSERT_FALSE(adapter.GetRevision().ok());
  ASSERT_FALSE(adapter.GetModel().ok());
  ASSERT_FALSE(adapter.GetFirmwareVersion().ok());

  EXPECT_EQ(StatusCode::kUnavailable, adapter.GetVendorId().status().code());
  EXPECT_EQ(StatusCode::kUnavailable, adapter.GetProductId().status().code());
  EXPECT_EQ(StatusCode::kUnavailable, adapter.GetRevision().status().code());
  EXPECT_EQ(StatusCode::kUnavailable, adapter.GetModel().status().code());
  EXPECT_EQ(StatusCode::kUnavailable,
            adapter.GetFirmwareVersion().status().code());
}

}  // namespace diagnostics
