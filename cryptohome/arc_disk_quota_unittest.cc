// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/arc_disk_quota.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"

#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/quota.h>
#include <sys/types.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Ne;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArrayArgument;

namespace cryptohome {

namespace {

constexpr char kDev[] = "/dev/mmcblk0p1";

}  // namespace

class ArcDiskQuotaTest : public ::testing::Test {
 public:
  ArcDiskQuotaTest()
      : arc_disk_quota_(&homedirs_, &platform_, base::FilePath(kArcDiskHome)) {}
  ArcDiskQuotaTest(const ArcDiskQuotaTest&) = delete;
  ArcDiskQuotaTest& operator=(const ArcDiskQuotaTest&) = delete;

  ~ArcDiskQuotaTest() override {}

 protected:
  MockHomeDirs homedirs_;
  MockPlatform platform_;
  ArcDiskQuota arc_disk_quota_;

  static const uid_t kAndroidUidStart = ArcDiskQuota::kAndroidUidStart;
  static const uid_t kAndroidUidEnd = ArcDiskQuota::kAndroidUidEnd;
  static const gid_t kAndroidGidStart = ArcDiskQuota::kAndroidGidStart;
  static const gid_t kAndroidGidEnd = ArcDiskQuota::kAndroidGidEnd;
  static const uid_t kValidAndroidUid = (kAndroidUidStart + kAndroidUidEnd) / 2;
  static const gid_t kValidAndroidGid = (kAndroidGidStart + kAndroidGidEnd) / 2;
};

TEST_F(ArcDiskQuotaTest, QuotaIsSupported) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  // Exactly 1 Android user.
  EXPECT_CALL(homedirs_, GetUnmountedAndroidDataCount()).WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(true, arc_disk_quota_.IsQuotaSupported());
}

TEST_F(ArcDiskQuotaTest, QuotaIsNotSupported_NoDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(false)));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(false, arc_disk_quota_.IsQuotaSupported());
}

TEST_F(ArcDiskQuotaTest, QuotaIsNotSupported_NoQuotaMountedDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(-1));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(false, arc_disk_quota_.IsQuotaSupported());
}

TEST_F(ArcDiskQuotaTest, QuotaIsNotSupported_MultipleAndroidUser) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  // Multiple Android users.
  EXPECT_CALL(homedirs_, GetUnmountedAndroidDataCount()).WillOnce(Return(2));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(false, arc_disk_quota_.IsQuotaSupported());
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_Succeeds) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(
                             base::FilePath(kDev),
                             kValidAndroidUid + kArcContainerShiftUid))
      .WillOnce(Return(5));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(5, arc_disk_quota_.GetCurrentSpaceForUid(kValidAndroidUid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_UidTooSmall) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForUid(kAndroidUidStart - 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_UidTooLarge) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForUid(kAndroidUidEnd + 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_NoDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(false)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(_, _)).Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForUid(kValidAndroidUid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_NoQuotaMountedDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(-1));

  EXPECT_CALL(platform_,
              GetQuotaCurrentSpaceForUid(Ne(base::FilePath(kDev)), Ne(0)))
      .Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForUid(kValidAndroidUid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForUid_QuotactlFails) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(
                             base::FilePath(kDev),
                             kValidAndroidUid + kArcContainerShiftUid))
      .WillOnce(Return(-1));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForUid(kValidAndroidUid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_Succeeds) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForGid(
                             base::FilePath(kDev),
                             kValidAndroidGid + kArcContainerShiftGid))
      .WillOnce(Return(5));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(5, arc_disk_quota_.GetCurrentSpaceForGid(kValidAndroidGid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_GidTooSmall) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForGid(kAndroidGidStart - 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_GidTooLarge) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForGid(kAndroidGidEnd + 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_NoDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(false)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForGid(_, _)).Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForGid(kValidAndroidGid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_NoQuotaMountedDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(-1));

  EXPECT_CALL(platform_,
              GetQuotaCurrentSpaceForUid(Ne(base::FilePath(kDev)), Ne(0)))
      .Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForGid(kValidAndroidGid));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForGid_QuotactlFails) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForGid(
                             base::FilePath(kDev),
                             kValidAndroidGid + kArcContainerShiftGid))
      .WillOnce(Return(-1));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForGid(kValidAndroidGid));
}

}  // namespace cryptohome
