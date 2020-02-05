// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/rar_manager.h"

#include <brillo/process_reaper.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"

namespace cros_disks {
namespace {

const char kMountRootDirectory[] = "/my_mount_point";

}  // namespace

class RarManagerTest : public testing::Test {
 protected:
  Metrics metrics_;
  Platform platform_;
  brillo::ProcessReaper reaper_;
  const RarManager manager_{kMountRootDirectory, &platform_, &metrics_,
                            &reaper_};
};

TEST_F(RarManagerTest, CanMount) {
  const MountManager& m = manager_;
  EXPECT_FALSE(m.CanMount(""));
  EXPECT_FALSE(m.CanMount(".rar"));
  EXPECT_FALSE(m.CanMount("blah.rar"));
  EXPECT_FALSE(m.CanMount("/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/x/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/Downloads/blah.rar"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/Downloads/x/blah.rar"));
  EXPECT_FALSE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/x/blah.rar"));
  EXPECT_FALSE(m.CanMount("/home/chronos/user/MyFiles/blah.rar"));
  EXPECT_FALSE(
      m.CanMount("/home/x/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/archive/y/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/removable/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/x/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/x/blah.rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/Blah.Rar"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/BLAH.RAR"));
  EXPECT_FALSE(m.CanMount("x/media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("/media/fuse/y/blah.ram"));
  EXPECT_FALSE(m.CanMount("file:///media/fuse/y/blah.rar"));
  EXPECT_FALSE(m.CanMount("ssh:///media/fuse/y/blah.rar"));
}

TEST_F(RarManagerTest, SuggestMountPath) {
  const RarManager& m = manager_;
  const std::string expected_mount_path =
      std::string(kMountRootDirectory) + "/doc.rar";
  EXPECT_EQ(m.SuggestMountPath("/home/chronos/user/Downloads/doc.rar"),
            expected_mount_path);
  EXPECT_EQ(m.SuggestMountPath("/media/archive/test.rar/doc.rar"),
            expected_mount_path);
}

}  // namespace cros_disks
