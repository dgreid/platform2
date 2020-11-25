// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/arc_disk_quota.h"
#include "cryptohome/mock_homedirs.h"
#include "cryptohome/mock_platform.h"
#include "cryptohome/projectid_config.h"

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
  static const int kValidAndroidProjectId =
      (kProjectIdForAndroidFilesStart + kProjectIdForAndroidFilesEnd) / 2;
  static constexpr char kObfuscatedUsername[] = "cafef00d";
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

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_Succeeds) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForProjectId(
                             base::FilePath(kDev), kValidAndroidProjectId))
      .WillOnce(Return(5));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(
      5, arc_disk_quota_.GetCurrentSpaceForProjectId(kValidAndroidProjectId));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_IdTooSmall) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForProjectId(
                    kProjectIdForAndroidFilesStart - 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_IdTooLarge) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(-1, arc_disk_quota_.GetCurrentSpaceForProjectId(
                    kProjectIdForAndroidFilesEnd + 1));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_NoDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(false)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForProjectId(_, _)).Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(
      -1, arc_disk_quota_.GetCurrentSpaceForProjectId(kValidAndroidProjectId));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_NoQuotaMountedDevice) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(-1));

  EXPECT_CALL(platform_,
              GetQuotaCurrentSpaceForUid(Ne(base::FilePath(kDev)), Ne(0)))
      .Times(0);

  arc_disk_quota_.Initialize();
  EXPECT_EQ(
      -1, arc_disk_quota_.GetCurrentSpaceForProjectId(kValidAndroidProjectId));
}

TEST_F(ArcDiskQuotaTest, GetCurrentSpaceForProjectId_QuotactlFails) {
  EXPECT_CALL(platform_, FindFilesystemDevice(base::FilePath(kArcDiskHome), _))
      .WillOnce(DoAll(SetArgPointee<1>(kDev), Return(true)));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForUid(base::FilePath(kDev), 0))
      .WillOnce(Return(0));

  EXPECT_CALL(platform_, GetQuotaCurrentSpaceForProjectId(
                             base::FilePath(kDev), kValidAndroidProjectId))
      .WillOnce(Return(-1));

  arc_disk_quota_.Initialize();
  EXPECT_EQ(
      -1, arc_disk_quota_.GetCurrentSpaceForProjectId(kValidAndroidProjectId));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_Succeeds) {
  constexpr int kProjectId = kValidAndroidProjectId;
  const auto kParentPath = SetProjectIdAllowedPathType::PATH_DOWNLOADS;
  const auto kChildPath = base::FilePath("test.png");
  const base::FilePath kExpectedPath =
      base::FilePath("/home/user/cafef00d/Downloads/test.png");

  EXPECT_CALL(homedirs_, CryptohomeExists(kObfuscatedUsername))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, kExpectedPath))
      .WillOnce(Return(true));

  EXPECT_TRUE(arc_disk_quota_.SetProjectId(kProjectId, kParentPath, kChildPath,
                                           kObfuscatedUsername));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_IdOutOfAllowedRange) {
  constexpr int kProjectId = kProjectIdForAndroidFilesEnd + 1;
  const auto kParentPath = SetProjectIdAllowedPathType::PATH_DOWNLOADS;
  const auto kChildPath = base::FilePath("test.png");

  EXPECT_CALL(homedirs_, CryptohomeExists(_)).Times(0);
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, _)).Times(0);

  EXPECT_FALSE(arc_disk_quota_.SetProjectId(kProjectId, kParentPath, kChildPath,
                                            kObfuscatedUsername));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_InvalidPath) {
  constexpr int kProjectId = kValidAndroidProjectId;
  const auto kParentPath = SetProjectIdAllowedPathType::PATH_DOWNLOADS;
  // Child path contains ".."
  const auto kChildPath = base::FilePath("/../test.png");

  EXPECT_CALL(homedirs_, CryptohomeExists(_)).Times(0);
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, _)).Times(0);

  EXPECT_FALSE(arc_disk_quota_.SetProjectId(kProjectId, kParentPath, kChildPath,
                                            kObfuscatedUsername));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_InvalidParentPathType) {
  constexpr int kProjectId = kValidAndroidProjectId;
  const auto kInvalidParentPath = static_cast<SetProjectIdAllowedPathType>(3);
  const auto kChildPath = base::FilePath("test.png");

  EXPECT_CALL(homedirs_, CryptohomeExists(kObfuscatedUsername))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, _)).Times(0);

  EXPECT_FALSE(arc_disk_quota_.SetProjectId(kProjectId, kInvalidParentPath,
                                            kChildPath, kObfuscatedUsername));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_CryptohomeNotExist) {
  constexpr int kProjectId = kValidAndroidProjectId;
  const auto kParentPath = SetProjectIdAllowedPathType::PATH_DOWNLOADS;
  const auto kChildPath = base::FilePath("test.png");
  const auto kInvalidObfuscatedUsername = "deadbeef";

  EXPECT_CALL(homedirs_, CryptohomeExists(kInvalidObfuscatedUsername))
      .WillOnce(Return(false));
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, _)).Times(0);

  EXPECT_FALSE(arc_disk_quota_.SetProjectId(kProjectId, kParentPath, kChildPath,
                                            kInvalidObfuscatedUsername));
}

TEST_F(ArcDiskQuotaTest, SetProjectId_IoctlFails) {
  constexpr int kProjectId = kValidAndroidProjectId;
  const auto kParentPath = SetProjectIdAllowedPathType::PATH_DOWNLOADS;
  const auto kChildPath = base::FilePath("test.png");

  EXPECT_CALL(homedirs_, CryptohomeExists(kObfuscatedUsername))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, SetQuotaProjectId(kProjectId, _))
      .WillOnce(Return(false));

  EXPECT_FALSE(arc_disk_quota_.SetProjectId(kProjectId, kParentPath, kChildPath,
                                            kObfuscatedUsername));
}

}  // namespace cryptohome
