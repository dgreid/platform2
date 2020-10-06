// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/system_mounter.h"

#include <sys/mount.h>

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"

namespace cros_disks {

namespace {
class PlatformForTest : public Platform {
 public:
  // Tests are being run on devices that don't support nosymfollow. Strip it.
  MountErrorType Mount(const std::string& source,
                       const std::string& target,
                       const std::string& filesystem_type,
                       uint64_t flags,
                       const std::string& options) const override {
    EXPECT_TRUE((flags & MS_NOSYMFOLLOW) == MS_NOSYMFOLLOW);
    return Platform::Mount(source, target, filesystem_type,
                           flags & ~MS_NOSYMFOLLOW, options);
  }
};
}  // namespace

TEST(SystemMounterTest, RunAsRootMount) {
  PlatformForTest platform;
  SystemMounter mounter(&platform, "tmpfs", false, {});

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  MountErrorType error = MOUNT_ERROR_NONE;
  auto mountpoint = mounter.Mount("/dev/null", temp_dir.GetPath(), {}, &error);
  EXPECT_TRUE(mountpoint);
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
  error = mountpoint->Unmount();
  EXPECT_EQ(MOUNT_ERROR_NONE, error);
}

TEST(SystemMounterTest, RunAsRootMountWithNonexistentSourcePath) {
  PlatformForTest platform;
  SystemMounter mounter(&platform, "ext2", false, {});

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // To test mounting a nonexistent source path, use ext2 as the
  // filesystem type instead of tmpfs since tmpfs does not care
  // about source path.
  MountErrorType error = MOUNT_ERROR_NONE;
  auto mountpoint =
      mounter.Mount("/nonexistent", temp_dir.GetPath(), {}, &error);
  EXPECT_FALSE(mountpoint);
  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH, error);
}

TEST(SystemMounterTest, RunAsRootMountWithNonexistentTargetPath) {
  PlatformForTest platform;
  SystemMounter mounter(&platform, "tmpfs", false, {});

  MountErrorType error = MOUNT_ERROR_NONE;
  auto mountpoint =
      mounter.Mount("/dev/null", base::FilePath("/nonexistent"), {}, &error);
  EXPECT_FALSE(mountpoint);
  EXPECT_EQ(MOUNT_ERROR_INVALID_PATH, error);
}

TEST(SystemMounterTest, RunAsRootMountWithNonexistentFilesystemType) {
  PlatformForTest platform;
  SystemMounter mounter(&platform, "nonexistentfs", false, {});

  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  MountErrorType error = MOUNT_ERROR_NONE;
  auto mountpoint = mounter.Mount("/dev/null", temp_dir.GetPath(), {}, &error);
  EXPECT_FALSE(mountpoint);
  EXPECT_EQ(MOUNT_ERROR_UNSUPPORTED_FILESYSTEM, error);
}

}  // namespace cros_disks
