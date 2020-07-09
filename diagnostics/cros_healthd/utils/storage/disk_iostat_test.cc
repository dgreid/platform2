// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/disk_iostat.h"

namespace diagnostics {

// Tests that all fields 4.18+ kernel are populated.
TEST(DiskIoStat, Extended) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n1";
  DiskIoStat iostat{base::FilePath(kPath)};
  ASSERT_TRUE(iostat.Update().ok());

  EXPECT_EQ(144016, iostat.GetReadTime().InMilliseconds());
  EXPECT_EQ(22155414, iostat.GetWriteTime().InMilliseconds());
  EXPECT_EQ(35505772, iostat.GetReadSectors());
  EXPECT_EQ(665648234, iostat.GetWrittenSectors());
  EXPECT_EQ(4646032, iostat.GetIoTime().InMilliseconds());
  ASSERT_TRUE(iostat.GetDiscardTime().has_value());
  EXPECT_EQ(200092, iostat.GetDiscardTime().value().InMilliseconds());
}

// Tests that some fields are correctly missing on <4.18 kernel.
TEST(DiskIoStat, Legacy) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/mmcblk0";
  DiskIoStat iostat{base::FilePath(kPath)};
  ASSERT_TRUE(iostat.Update().ok());

  EXPECT_EQ(184023, iostat.GetReadTime().InMilliseconds());
  EXPECT_EQ(13849275, iostat.GetWriteTime().InMilliseconds());
  EXPECT_EQ(84710472, iostat.GetReadSectors());
  EXPECT_EQ(7289304, iostat.GetWrittenSectors());
  EXPECT_EQ(7392983, iostat.GetIoTime().InMilliseconds());
  EXPECT_FALSE(iostat.GetDiscardTime().has_value());
}

// Tests missing stat file.
TEST(DiskIoStat, NotFound) {
  constexpr char kPath[] = "cros_healthd/utils/storage/testdata/sys/block/sda1";
  DiskIoStat iostat{base::FilePath(kPath)};
  auto status = iostat.Update();
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(StatusCode::kUnavailable, status.code());
}

// Tests mis-formatted stat file.
TEST(DiskIoStat, WrongFormat) {
  constexpr char kPath[] =
      "cros_healthd/utils/storage/testdata/sys/block/nvme0n2";
  DiskIoStat iostat{base::FilePath(kPath)};
  auto status = iostat.Update();
  ASSERT_FALSE(status.ok());
  EXPECT_EQ(StatusCode::kInvalidArgument, status.code());
}

}  // namespace diagnostics
