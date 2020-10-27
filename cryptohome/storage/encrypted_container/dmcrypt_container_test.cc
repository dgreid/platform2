// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/dmcrypt_container.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <brillo/blkdev_utils/device_mapper_fake.h>
#include <brillo/secure_blob.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/encrypted_container.h"
#include "cryptohome/storage/encrypted_container/fake_backing_device.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace cryptohome {

class DmcryptContainerTest : public ::testing::Test {
 public:
  DmcryptContainerTest()
      : config_({.dmcrypt_device_name = "crypt_device",
                 .dmcrypt_cipher = "aes-xts-plain64",
                 .mkfs_opts = {"-O", "encrypt,verity"},
                 .tune2fs_opts = {"-Q", "project"}}),
        key_({.fek = brillo::SecureBlob("random key")}),
        device_mapper_(base::Bind(&brillo::fake::CreateDevmapperTask)),
        backing_device_(std::make_unique<FakeBackingDevice>(
            BackingDeviceType::kLogicalVolumeBackingDevice,
            base::FilePath("/dev/VG/LV"))) {}
  ~DmcryptContainerTest() override = default;

  void GenerateContainer() {
    container_ = std::make_unique<DmcryptContainer>(
        config_, std::move(backing_device_), key_reference_, &platform_,
        std::make_unique<brillo::DeviceMapper>(
            base::Bind(&brillo::fake::CreateDevmapperTask)));
  }

 protected:
  DmcryptConfig config_;

  FileSystemKeyReference key_reference_;
  FileSystemKey key_;
  MockPlatform platform_;
  brillo::DeviceMapper device_mapper_;
  std::unique_ptr<BackingDevice> backing_device_;
  std::unique_ptr<DmcryptContainer> container_;
};

// Tests the creation path for the dm-crypt container.
TEST_F(DmcryptContainerTest, SetupCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_, /*create=*/true));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_.fek);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmcrypt_device_name));
}

// Tests the setup path with an existing container.
TEST_F(DmcryptContainerTest, SetupNoCreateCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_, /*create=*/false));
  // Check that the device mapper target exists.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            key_.fek);
  EXPECT_TRUE(device_mapper_.Remove(config_.dmcrypt_device_name));
}

// Tests failure path if the backing device fails to setup.
TEST_F(DmcryptContainerTest, SetupFailedBackingDeviceSetup) {
  GenerateContainer();

  EXPECT_FALSE(container_->Setup(key_, /*create=*/false));
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

// Tests failure path if the filesystem setup fails.
TEST_F(DmcryptContainerTest, SetupFailedFormatExt4) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, FormatExt4(_, _, _)).WillOnce(Return(false));

  GenerateContainer();

  EXPECT_FALSE(container_->Setup(key_, /*create=*/true));
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

// Tests the failure path on setting new filesystem features.
TEST_F(DmcryptContainerTest, SetupFailedTune2fs) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(false));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_FALSE(container_->Setup(key_, /*create=*/false));
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

// Tests that teardown doesn't leave an active dm-crypt device or an attached
// backing device.
TEST_F(DmcryptContainerTest, TeardownCheck) {
  EXPECT_CALL(platform_, GetBlkSize(_, _))
      .WillOnce(DoAll(SetArgPointee<1>(1024 * 1024 * 1024), Return(true)));
  EXPECT_CALL(platform_, UdevAdmSettle(_, _)).WillOnce(Return(true));
  EXPECT_CALL(platform_, Tune2Fs(_, _)).WillOnce(Return(true));

  backing_device_->Create();
  GenerateContainer();

  EXPECT_TRUE(container_->Setup(key_, /*create=*/false));
  // Now, attempt teardown of the device.
  EXPECT_TRUE(container_->Teardown());
  // Check that the device mapper target doesn't exist.
  EXPECT_EQ(device_mapper_.GetTable(config_.dmcrypt_device_name).CryptGetKey(),
            brillo::SecureBlob());
}

}  // namespace cryptohome
