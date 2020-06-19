// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <gtest/gtest.h>

namespace cros_disks {

TEST(ArchiveManagerTest, IsInAllowedFolder) {
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/dev/sda1"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/devices/block/sda/sda1"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/sys/devices/block/sda/sda1"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/removable/foo"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/removable/foo/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/archive/foo"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/archive/foo/"));
  EXPECT_TRUE(ArchiveManager::IsInAllowedFolder("/media/removable/foo/bar"));
  EXPECT_TRUE(
      ArchiveManager::IsInAllowedFolder("/media/removable/foo/dir1/bar"));
  EXPECT_TRUE(ArchiveManager::IsInAllowedFolder("/media/removable/foo/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/user/Downloads/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/user/GCache/bar"));
  EXPECT_TRUE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/MyFiles/"
      "Downloads/bar"));
  EXPECT_TRUE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/MyFiles/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(""));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/tmp"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/removable"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/removable/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/archive"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/media/archive/"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/user/Downloads"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/user/Downloads/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/home/chronos/user/GCache"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/home/chronos/user/GCache/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/Downloads"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/Downloads/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/GCache"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/GCache/"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/home/chronos/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/home/chronos/user/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef01234567/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/Downloads/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder("/home/chronos/GCache/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/foo/Downloads/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/foo/GCache/bar"));
  EXPECT_FALSE(
      ArchiveManager::IsInAllowedFolder("/home/chronos/u-/Downloads/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-0123456789abcdef0123456789abcdef0123456/Downloads/bar"));
  EXPECT_FALSE(ArchiveManager::IsInAllowedFolder(
      "/home/chronos/u-xyz3456789abcdef0123456789abcdef01234567/Downloads/"
      "bar"));
}

}  // namespace cros_disks
