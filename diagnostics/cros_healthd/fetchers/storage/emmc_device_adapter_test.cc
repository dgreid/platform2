// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>

#include <gtest/gtest.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/fetchers/storage/emmc_device_adapter.h"

namespace diagnostics {

TEST(EmmcDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk0";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_emmc_oemid());
  ASSERT_TRUE(adapter.GetProductId().value().is_emmc_pnm());
  ASSERT_TRUE(adapter.GetRevision().value().is_emmc_prv());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_emmc_fwrev());

  EXPECT_EQ("mmcblk0", adapter.GetDeviceName());
  EXPECT_EQ(0x5050, adapter.GetVendorId().value().get_emmc_oemid());
  EXPECT_EQ(0x4D4E504D4E50, adapter.GetProductId().value().get_emmc_pnm());
  EXPECT_EQ(0x8, adapter.GetRevision().value().get_emmc_prv());
  EXPECT_EQ("PNMPNM", adapter.GetModel().value());
  EXPECT_EQ(0x1223344556677889,
            adapter.GetFirmwareVersion().value().get_emmc_fwrev());
}

TEST(EmmcDeviceAdapterTest, OldMmc) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk2";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_emmc_oemid());
  ASSERT_TRUE(adapter.GetProductId().value().is_emmc_pnm());
  ASSERT_TRUE(adapter.GetRevision().value().is_emmc_prv());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_emmc_fwrev());

  EXPECT_EQ("mmcblk2", adapter.GetDeviceName());
  EXPECT_EQ(0x5050, adapter.GetVendorId().value().get_emmc_oemid());
  EXPECT_EQ(0x4D4E504D4E50, adapter.GetProductId().value().get_emmc_pnm());
  EXPECT_EQ(0x4, adapter.GetRevision().value().get_emmc_prv());
  EXPECT_EQ("PNMPNM", adapter.GetModel().value());
  EXPECT_EQ(0x1223344556677889,
            adapter.GetFirmwareVersion().value().get_emmc_fwrev());
}

// Test when device is present, but data is missing.
TEST(EmmcDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/mmcblk1";
  EmmcDeviceAdapter adapter{base::FilePath(kPath)};

  EXPECT_EQ("mmcblk1", adapter.GetDeviceName());

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
