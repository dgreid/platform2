// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/zip_manager.h"

#include <brillo/process/process_reaper.h>
#include <gtest/gtest.h>

#include "cros-disks/metrics.h"
#include "cros-disks/platform.h"

namespace cros_disks {
namespace {

const char kMountRootDirectory[] = "/my_mount_point";

}  // namespace

class ZipManagerTest : public testing::Test {
 protected:
  Metrics metrics_;
  Platform platform_;
  brillo::ProcessReaper reaper_;
  const ZipManager manager_{kMountRootDirectory, &platform_, &metrics_,
                            &reaper_};
};

TEST_F(ZipManagerTest, CanMount) {
  const MountManager& m = manager_;
  EXPECT_FALSE(m.CanMount(""));
  EXPECT_FALSE(m.CanMount(".zip"));
  EXPECT_FALSE(m.CanMount("blah.zip"));
  EXPECT_FALSE(m.CanMount("/blah.zip"));
  EXPECT_TRUE(
      m.CanMount("/home/chronos/u-0123456789abcdef0123456789abcdef01234567"
                 "/MyFiles/blah.zip"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/blah.zip"));
  EXPECT_TRUE(m.CanMount("/media/removable/y/blah.zip"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/Blah.Zip"));
  EXPECT_TRUE(m.CanMount("/media/fuse/y/BLAH.ZIP"));
  EXPECT_FALSE(m.CanMount("/media/fuse/y/blah.zipx"));
}

}  // namespace cros_disks
