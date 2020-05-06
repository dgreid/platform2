// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/disk_cleanup.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_disk_cleanup_routines.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/mock_user_oldest_activity_timestamp_cache.h"

using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::StrictMock;

namespace cryptohome {

TEST(DiskCleanupInitialization, Init) {
  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;
  StrictMock<UserOldestActivityTimestampCache> timestamp_cache_;

  DiskCleanup cleanup;

  EXPECT_TRUE(cleanup.Init(&homedirs_, &platform_, &timestamp_cache_));
  cleanup.set_routines_for_testing(new StrictMock<MockDiskCleanupRoutines>);
}

class DiskCleanupTest : public ::testing::Test {
 public:
  DiskCleanupTest() = default;

  void SetUp() {
    cleanup_.reset(new DiskCleanup());
    ASSERT_TRUE(cleanup_->Init(&homedirs_, &platform_, &timestamp_cache_));

    cleanup_routines_ = new StrictMock<MockDiskCleanupRoutines>;
    cleanup_->set_routines_for_testing(cleanup_routines_);

    EXPECT_CALL(homedirs_, shadow_root)
        .WillRepeatedly(ReturnRef(kTestShadowRoot));

    EXPECT_CALL(platform_, GetCurrentTime).WillRepeatedly(Return(base::Time()));
  }

 protected:
  base::FilePath kTestShadowRoot = base::FilePath("/test/shadow/root");

  StrictMock<MockPlatform> platform_;
  StrictMock<MockHomeDirs> homedirs_;
  StrictMock<UserOldestActivityTimestampCache> timestamp_cache_;
  StrictMock<MockDiskCleanupRoutines>* cleanup_routines_;
  std::unique_ptr<DiskCleanup> cleanup_;
};

TEST_F(DiskCleanupTest, EphemeralUsers) {
  EXPECT_CALL(platform_, AmountOfFreeDiskSpace(kTestShadowRoot))
      .WillRepeatedly(Return(kFreeSpaceThresholdToTriggerCleanup - 1));

  EXPECT_CALL(homedirs_, AreEphemeralUsersEnabled()).WillOnce(Return(true));

  EXPECT_CALL(homedirs_, RemoveNonOwnerCryptohomes()).WillOnce(Return());

  cleanup_->FreeDiskSpace();
}

}  // namespace cryptohome
