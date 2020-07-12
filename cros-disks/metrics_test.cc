// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/metrics.h"

#include <gtest/gtest.h>

namespace cros_disks {

class MetricsTest : public ::testing::Test {
 protected:
  Metrics metrics_;
};

TEST_F(MetricsTest, GetArchiveType) {
  EXPECT_EQ(Metrics::kArchiveUnknown, metrics_.GetArchiveType(""));
  EXPECT_EQ(Metrics::kArchiveUnknown, metrics_.GetArchiveType("txt"));
  EXPECT_EQ(Metrics::kArchiveZip, metrics_.GetArchiveType("zip"));
  EXPECT_EQ(Metrics::kArchiveRar, metrics_.GetArchiveType("rar"));
}

TEST_F(MetricsTest, GetFilesystemType) {
  EXPECT_EQ(Metrics::kFilesystemUnknown, metrics_.GetFilesystemType(""));
  EXPECT_EQ(Metrics::kFilesystemVFAT, metrics_.GetFilesystemType("vfat"));
  EXPECT_EQ(Metrics::kFilesystemExFAT, metrics_.GetFilesystemType("exfat"));
  EXPECT_EQ(Metrics::kFilesystemNTFS, metrics_.GetFilesystemType("ntfs"));
  EXPECT_EQ(Metrics::kFilesystemHFSPlus, metrics_.GetFilesystemType("hfsplus"));
  EXPECT_EQ(Metrics::kFilesystemExt2, metrics_.GetFilesystemType("ext2"));
  EXPECT_EQ(Metrics::kFilesystemExt3, metrics_.GetFilesystemType("ext3"));
  EXPECT_EQ(Metrics::kFilesystemExt4, metrics_.GetFilesystemType("ext4"));
  EXPECT_EQ(Metrics::kFilesystemISO9660, metrics_.GetFilesystemType("iso9660"));
  EXPECT_EQ(Metrics::kFilesystemUDF, metrics_.GetFilesystemType("udf"));
  EXPECT_EQ(Metrics::kFilesystemOther, metrics_.GetFilesystemType("xfs"));
  EXPECT_EQ(Metrics::kFilesystemOther, metrics_.GetFilesystemType("btrfs"));
}

}  // namespace cros_disks
