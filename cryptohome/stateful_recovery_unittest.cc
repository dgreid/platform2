// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/stateful_recovery.h"

#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include <memory>

#include "cryptohome/mock_platform.h"

namespace cryptohome {

using base::FilePath;
using std::ostringstream;

using ::testing::DoAll;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::_;

// MockSRHandlers is a class that contains the 3 functions required to create
// the StatefulRecovery object. This mock object is created to simplify testing.
class MockSRHandlers {
 public:
  MOCK_METHOD(bool,
              Mount,
              (const std::string& username,
               const std::string& passkey,
               base::FilePath* out_home_path),
              ());
  MOCK_METHOD(bool, Unmount, (), ());
  MOCK_METHOD(bool, IsOwner, (const std::string& username), ());
};

// StatefulRecoveryTest is a test fixture for all Stateful Recovery unit tests.
class StatefulRecoveryTest : public ::testing::Test {
 public:
  StatefulRecoveryTest() {}

  ~StatefulRecoveryTest() override = default;

  void SetUp() override {
    platform_.reset(new MockPlatform());
    handlers_.reset(new MockSRHandlers());
  }

  void Initialize() {
    auto mount =
        base::Bind(&MockSRHandlers::Mount, base::Unretained(handlers_.get()));
    auto unmount =
        base::Bind(&MockSRHandlers::Unmount, base::Unretained(handlers_.get()));
    auto is_owner =
        base::Bind(&MockSRHandlers::IsOwner, base::Unretained(handlers_.get()));
    recovery_.reset(
        new StatefulRecovery(platform_.get(), mount, unmount, is_owner));
  }

 protected:
  // Handlers for Mount, Unmount and IsOwner.
  std::unique_ptr<MockSRHandlers> handlers_;

  // Mock platform object.
  std::unique_ptr<MockPlatform> platform_;

  // The Stateful Recovery that we want to test.
  std::unique_ptr<StatefulRecovery> recovery_;
};

TEST_F(StatefulRecoveryTest, ValidRequestV1) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV1WriteProtected) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  FilePath mount_path("/home/.shadow/hashhashash/mount");
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(*handlers_, Mount(StrEq(user), StrEq(passkey), _))
      .WillOnce(DoAll(SetArgPointee<2>(mount_path), Return(true)));
  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*handlers_, Unmount()).WillOnce(Return(true));

  EXPECT_CALL(*handlers_, IsOwner(_)).WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2NotOwner) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  FilePath mount_path("/home/.shadow/hashhashash/mount");
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(*handlers_, Mount(StrEq(user), StrEq(passkey), _))
      .WillOnce(DoAll(SetArgPointee<2>(mount_path), Return(true)));
  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*handlers_, Unmount()).WillOnce(Return(true));

  EXPECT_CALL(*handlers_, IsOwner(_)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2BadUser) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(*handlers_, Mount(StrEq(user), StrEq(passkey), _))
      .WillOnce(Return(false));

  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2BadUserNotWriteProtected) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  FilePath mount_path("/home/.shadow/hashhashash/mount");
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(*handlers_, Mount(StrEq(user), StrEq(passkey), _))
      .WillOnce(Return(false));

  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, ValidRequestV2NotOwnerNotWriteProtected) {
  std::string user = "user@example.com";
  std::string passkey = "abcd1234";
  std::string flag_content = "2\n" + user + "\n" + passkey;
  FilePath mount_path("/home/.shadow/hashhashash/mount");
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  // CopyUserContents
  EXPECT_CALL(*handlers_, Mount(StrEq(user), StrEq(passkey), _))
      .WillOnce(DoAll(SetArgPointee<2>(mount_path), Return(true)));
  EXPECT_CALL(*platform_,
              Copy(mount_path, FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*handlers_, Unmount()).WillOnce(Return(true));

  EXPECT_CALL(*handlers_, IsOwner(_)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));

  // CopyPartitionInfo
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(true));

  // CopyPartitionContents
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_TRUE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, InvalidFlagFileContents) {
  std::string flag_content = "0 hello";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  Initialize();
  EXPECT_FALSE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, UnreadableFlagFile) {
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(Return(false));
  Initialize();
  EXPECT_FALSE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, UncopyableData) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, DirectoryCreationFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, StatVFSFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, FilesystemDetailsFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, FirmwareWriteProtected()).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Copy(FilePath(StatefulRecovery::kRecoverSource),
                               FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              StatVFS(FilePath(StatefulRecovery::kRecoverSource), _))
      .WillOnce(Return(true));
  EXPECT_CALL(
      *platform_,
      WriteStringToFile(FilePath(StatefulRecovery::kRecoverBlockUsage), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              ReportFilesystemDetails(
                  FilePath(StatefulRecovery::kRecoverSource),
                  FilePath(StatefulRecovery::kRecoverFilesystemDetails)))
      .WillOnce(Return(false));

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

TEST_F(StatefulRecoveryTest, MountsParseOk) {
  Platform platform;
  FilePath mount_info;
  FILE *fp;
  std::string device_in = "/dev/pan", device_out, mount_info_contents;

  mount_info_contents.append("84 24 0:29 / ");
  FilePath filesystem = FilePath("/second/star/to/the/right");
  mount_info_contents.append(filesystem.value());
  mount_info_contents.append(" rw,nosuid,nodev,noexec,relatime - fairyfs ");
  mount_info_contents.append(device_in);
  mount_info_contents.append(" rw,ecryp...");

#if BASE_VER < 780000
  fp = base::CreateAndOpenTemporaryFile(&mount_info);
#else
  fp = base::CreateAndOpenTemporaryStream(&mount_info).release();
#endif
  ASSERT_TRUE(fp != NULL);
  EXPECT_EQ(fwrite(mount_info_contents.c_str(),
                   mount_info_contents.length(), 1, fp), 1);
  EXPECT_EQ(fclose(fp), 0);

  platform.set_mount_info_path(mount_info);

  /* Fails if item is missing. */
  EXPECT_FALSE(platform.FindFilesystemDevice(FilePath("monkey"), &device_out));

  /* Works normally. */
  device_out.clear();
  EXPECT_TRUE(platform.FindFilesystemDevice(filesystem, &device_out));
  EXPECT_TRUE(device_out == device_in);

  /* Clean up. */
  EXPECT_TRUE(base::DeleteFile(mount_info, false));
}

TEST_F(StatefulRecoveryTest, UsageReportOk) {
  Platform platform;

  struct statvfs vfs;
  /* Reporting on a valid location produces output. */
  EXPECT_TRUE(platform_->StatVFS(FilePath("/"), &vfs));
  EXPECT_NE(vfs.f_blocks, 0);

  /* Reporting on an invalid location fails. */
  EXPECT_FALSE(platform_->StatVFS(FilePath("/this/is/very/wrong"), &vfs));

  /* TODO(keescook): mockable tune2fs, since it's not installed in chroot. */
}

TEST_F(StatefulRecoveryTest, DestinationRecreateFailure) {
  std::string flag_content = "1";
  EXPECT_CALL(*platform_,
              ReadFileToString(FilePath(StatefulRecovery::kFlagFile), _))
      .WillOnce(DoAll(SetArgPointee<1>(flag_content), Return(true)));
  EXPECT_CALL(*platform_,
              DeleteFile(FilePath(StatefulRecovery::kRecoverDestination), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              CreateDirectory(FilePath(StatefulRecovery::kRecoverDestination)))
      .WillOnce(Return(false));
  EXPECT_CALL(*platform_,
              Copy(_, FilePath(StatefulRecovery::kRecoverDestination)))
      .Times(0);

  Initialize();
  EXPECT_TRUE(recovery_->Requested());
  EXPECT_FALSE(recovery_->Recover());
}

}  // namespace cryptohome
