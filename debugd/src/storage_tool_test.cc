// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/storage_tool.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>
#include <stdlib.h>

namespace {

constexpr char kMountsDataForNone[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
  )";

constexpr char kMountsDataForSamePrefix[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/sda1 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExample[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/sda1 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExampleMMC[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/mmcblk0p10 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExampleMMCRepeatedNumber[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/mmcblk3p3 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExampleDM[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/dm-1 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExampleNVME[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/nvme0n1p1 /mnt/stateful_partition rw 0 0
  )";

constexpr char kMountsDataExampleLOOP[] = R"(
/dev/root / ext2 rw 0 0
devtmpfs /dev devtmpfs rw 0 0
none /proc proc rw,nosuid,nodev,noexec,relatime 0 0
none /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0
none /sys/fs/selinux selinuxfs rw,nosuid,noexec,relatime 0 0
tmp /tmp tmpfs rw,seclabel,nodev,relatime 0 0
run /run tmpfs rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0
/dev/mapper/encstateful /mnt/stateful_partition/encrypted ext4 rw 0 0
/dev/loop1 /mnt/stateful_partition rw 0 0
  )";

constexpr char kTypeFileDataTarget[] = "/sys/devices/target/type";

constexpr char kTypeFileDataMMC[] = "/sys/devices/mmc_host/mmc0/type";

TEST(StorageToolTest, TestGetDevice) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExample, sizeof(kMountsDataExample));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/sda");
}

TEST(StorageToolTest, TestGetDeviceMMC) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExampleMMC, sizeof(kMountsDataExampleMMC));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/mmcblk0");
}

TEST(StorageToolTest, TestGetDeviceMMCRepeatedNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExampleMMCRepeatedNumber,
                  sizeof(kMountsDataExampleMMCRepeatedNumber));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/mmcblk3");
}

TEST(StorageToolTest, TestGetDeviceDM) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExampleDM, sizeof(kMountsDataExampleDM));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/dm-1");
}

TEST(StorageToolTest, TestGetDeviceNVME) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExampleNVME,
                  sizeof(kMountsDataExampleNVME));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/nvme0n1");
}

TEST(StorageToolTest, TestGetDeviceLoop) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataExampleLOOP,
                  sizeof(kMountsDataExampleLOOP));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/loop1");
}

TEST(StorageToolTest, TestGetDeviceNoMounts) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "");
}

TEST(StorageToolTest, TestGetDeviceForNone) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataForNone, sizeof(kMountsDataForNone));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "");
}

TEST(StorageToolTest, TestGetDeviceSamePrefix) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath mounts = temp_dir.GetPath().Append("mounts");
  base::WriteFile(mounts, kMountsDataForSamePrefix,
                  sizeof(kMountsDataForSamePrefix));

  debugd::StorageTool sTool;
  const base::FilePath device =
      sTool.GetDevice(base::FilePath("/mnt/stateful_partition"), mounts);
  EXPECT_STREQ(device.value().c_str(), "/dev/sda");
}

TEST(StorageToolTest, TestIsSupportedNoTypeLink) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to read device type link>");
}

TEST(StorageToolTest, TestIsSupportedMMC) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("mmc_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataMMC, sizeof(kTypeFileDataMMC));

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<This feature is not supported>");
}

TEST(StorageToolTest, TestIsSupportedNoVend) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget, sizeof(kTypeFileDataTarget));

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to open vendor file>");
}

TEST(StorageToolTest, TestIsSupportedVendEmpty) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget, sizeof(kTypeFileDataTarget));

  const char* vendData = "";
  base::WriteFile(vendFile, vendData, 0);

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to find device type>");
}

TEST(StorageToolTest, TestIsSupportedOther) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget, sizeof(kTypeFileDataTarget));

  constexpr char vendData[] = "OTHER";
  base::WriteFile(vendFile, vendData, sizeof(vendData));

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<This feature is not supported>");
}

TEST(StorageToolTest, TestIsSupportedATA) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget, sizeof(kTypeFileDataTarget));

  constexpr char vendData[] = "ATA";
  base::WriteFile(vendFile, vendData, sizeof(vendData));

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_TRUE(supported);
  EXPECT_STREQ(msg.c_str(), "");
}

}  // namespace
