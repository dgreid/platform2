// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mount_encrypted/encrypted_fs.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <brillo/blkdev_utils/loop_device_fake.h>
#include <cryptohome/cryptolib.h>
#include <cryptohome/mock_platform.h>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace mount_encrypted {

class EncryptedFsTest : public ::testing::Test {
 public:
  EncryptedFsTest()
      : device_mapper_(base::Bind(&brillo::fake::CreateDevmapperTask)) {
    CHECK(tmpdir_.CreateUniqueTempDir());
    CHECK(base::SetPosixFilePermissions(tmpdir_.GetPath(), 0707));
    CHECK(base::CreateDirectory(
        tmpdir_.GetPath().AppendASCII("mnt/stateful_partition/")));

    CHECK(base::CreateDirectory(tmpdir_.GetPath().AppendASCII("var/")));

    CHECK(base::CreateDirectory(tmpdir_.GetPath().AppendASCII("home/chronos")));

    platform_.GetFake()->SetStandardUsersAndGroups();

    // Setup EncryptedFs with temp directory.
    encrypted_fs_ = std::make_unique<EncryptedFs>(
        tmpdir_.GetPath(), &platform_, &loopdev_manager_, &device_mapper_);

    // Setup paths to check.
    brillo::SecureBlob digest = cryptohome::CryptoLib::Sha256(
        brillo::SecureBlob(tmpdir_.GetPath().value()));
    std::string hex = cryptohome::CryptoLib::SecureBlobToHex(digest);
    dmcrypt_name_ = "encstateful_" + hex.substr(0, 16);
    dmcrypt_device_ = base::FilePath(
        base::StringPrintf("/dev/mapper/%s", dmcrypt_name_.c_str()));
    mount_point_ = tmpdir_.GetPath().Append("mnt/stateful_partition/encrypted");

    // Set encryption key.
    brillo::SecureBlob::HexStringToSecureBlob("0123456789ABCDEF", &secret_);
  }

  cryptohome::MockPlatform platform_;
  base::ScopedTempDir tmpdir_;
  std::string dmcrypt_name_;
  base::FilePath dmcrypt_device_;
  base::FilePath loopback_device_;
  base::FilePath mount_point_;
  brillo::fake::FakeLoopDeviceManager loopdev_manager_;
  brillo::DeviceMapper device_mapper_;
  brillo::SecureBlob secret_;
  std::unique_ptr<EncryptedFs> encrypted_fs_;
};

TEST_F(EncryptedFsTest, RebuildStateful) {
  // Platform functions used
  EXPECT_CALL(platform_, CreateSparseFile(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, FormatExt4(dmcrypt_device_, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(40920000), Return(true)));
  EXPECT_CALL(platform_, Mount(dmcrypt_device_, mount_point_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _, _)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .Times(3)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Access(_, _)).WillRepeatedly(Return(0));

  // Check if dm device is mounted and has the correct key.
  EXPECT_EQ(encrypted_fs_->Setup(secret_, true), RESULT_SUCCESS);
  EXPECT_EQ(encrypted_fs_->GetKey(), secret_);

  // Check if loop device is mounted.
  std::unique_ptr<brillo::LoopDevice> loopdev =
      loopdev_manager_.GetAttachedDeviceByName(dmcrypt_name_);

  EXPECT_TRUE(loopdev->IsValid());
  EXPECT_EQ(encrypted_fs_->Teardown(), RESULT_SUCCESS);
  EXPECT_EQ(loopdev_manager_.GetAttachedDevices().size(), 0);
}

TEST_F(EncryptedFsTest, OldStateful) {
  // Create functions should never be called.
  EXPECT_CALL(platform_, CreateSparseFile(_, _)).Times(0);
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).Times(0);
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(40920000), Return(true)));
  EXPECT_CALL(platform_, Mount(dmcrypt_device_, mount_point_, _, _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(platform_, Bind(_, _, _)).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Unmount(_, _, _))
      .Times(3)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(platform_, Access(_, _)).WillRepeatedly(Return(0));

  // Expect setup to succeed.
  EXPECT_EQ(encrypted_fs_->Setup(secret_, false), RESULT_SUCCESS);
  EXPECT_EQ(encrypted_fs_->Teardown(), RESULT_SUCCESS);
}

TEST_F(EncryptedFsTest, LoopdevTeardown) {
  // BlkSize == 0 --> Teardown loopdev
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(0), Return(true)));

  // Expect setup to fail.
  EXPECT_EQ(encrypted_fs_->Setup(secret_, false), RESULT_FAIL_FATAL);
  // Make sure no loop device is left attached
  EXPECT_EQ(loopdev_manager_.GetAttachedDevices().size(), 0);
}

TEST_F(EncryptedFsTest, DevmapperTeardown) {
  // Mount failed --> Teardown devmapper
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(40920000), Return(true)));
  EXPECT_CALL(platform_, Mount(_, _, _, _, _)).WillOnce(Return(false));

  brillo::DevmapperTable empty_table(0, 0, "", brillo::SecureBlob());

  // Expect setup to fail.
  EXPECT_EQ(encrypted_fs_->Setup(secret_, false), RESULT_FAIL_FATAL);
  // Make sure no loop device is left attached.
  EXPECT_EQ(loopdev_manager_.GetAttachedDevices().size(), 0);
  // Make sure no devmapper device is left.
  EXPECT_EQ(device_mapper_.GetTable(dmcrypt_name_).ToSecureBlob(),
            empty_table.ToSecureBlob());
}

}  // namespace mount_encrypted
