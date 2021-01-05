// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_point.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace cros_disks {

namespace {

using testing::_;
using testing::Return;

constexpr char kMountPath[] = "/mount/path";

class MockMountPoint : public MountPoint {
 public:
  explicit MockMountPoint(const base::FilePath& path) : MountPoint({path}) {}
  ~MockMountPoint() override { DestructorUnmount(); }

  MOCK_METHOD(MountErrorType, UnmountImpl, (), (override));
};

}  // namespace

TEST(MountPointTest, Unmount) {
  const auto mount_path = base::FilePath(kMountPath);
  auto mount_point = std::make_unique<MockMountPoint>(mount_path);

  EXPECT_CALL(*mount_point, UnmountImpl())
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARCHIVE))
      .WillOnce(Return(MOUNT_ERROR_NONE));
  EXPECT_EQ(MOUNT_ERROR_INVALID_ARCHIVE, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_NONE, mount_point->Unmount());
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
}

TEST(MountPointTest, UnmountOnDestroy) {
  const auto mount_path = base::FilePath(kMountPath);
  auto mount_point = std::make_unique<MockMountPoint>(mount_path);

  EXPECT_CALL(*mount_point, UnmountImpl())
      .WillOnce(Return(MOUNT_ERROR_INVALID_ARCHIVE));
}

TEST(MountPointTest, LeakMount) {
  auto mount_point =
      std::make_unique<MockMountPoint>(base::FilePath(kMountPath));

  EXPECT_CALL(*mount_point, UnmountImpl()).Times(0);

  mount_point->Release();
  EXPECT_EQ(MOUNT_ERROR_PATH_NOT_MOUNTED, mount_point->Unmount());
}

}  // namespace cros_disks
