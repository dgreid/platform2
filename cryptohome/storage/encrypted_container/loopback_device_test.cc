// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/loopback_device.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/blkdev_utils/loop_device_fake.h>
#include <gtest/gtest.h>

#include "cryptohome/mock_platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"

namespace cryptohome {

class LoopbackDeviceTest : public ::testing::Test {
 public:
  LoopbackDeviceTest()
      : config_(
            {.type = BackingDeviceType::kLoopbackDevice,
             .name = "foo",
             .size = 1024 * 1024 * 1024,
             .loopback = {.backing_file_path = base::FilePath("/a.block")}}),
        backing_device_(std::make_unique<LoopbackDevice>(
            config_,
            &platform_,
            std::make_unique<brillo::fake::FakeLoopDeviceManager>())) {}
  ~LoopbackDeviceTest() override = default;

 protected:
  BackingDeviceConfig config_;
  MockPlatform platform_;
  std::unique_ptr<LoopbackDevice> backing_device_;
};

// Tests the successful creation of the loop device's backing sparse file.
TEST_F(LoopbackDeviceTest, LoopbackDeviceCreate) {
  EXPECT_TRUE(backing_device_->Create());

  // Check that the sparse file was created with the correct mode.
  EXPECT_TRUE(backing_device_->Exists());
  mode_t mode;
  ASSERT_TRUE(
      platform_.GetPermissions(config_.loopback.backing_file_path, &mode));
  EXPECT_EQ(mode, S_IRUSR | S_IWUSR);
}

// Tests purge of the backing sparse file.
TEST_F(LoopbackDeviceTest, LoopbackPurge) {
  EXPECT_TRUE(platform_.WriteFile(config_.loopback.backing_file_path,
                                  brillo::Blob(32, 0)));
  EXPECT_TRUE(backing_device_->Purge());
  EXPECT_FALSE(backing_device_->Exists());
}

// Tests setup for a loopback device succeeded.
TEST_F(LoopbackDeviceTest, LoopbackSetup) {
  EXPECT_TRUE(backing_device_->Setup());

  EXPECT_NE(backing_device_->GetPath(), base::nullopt);
  EXPECT_TRUE(backing_device_->Teardown());
}

// Tests teardown of a loopback device doesn't leave the loop device attached.
TEST_F(LoopbackDeviceTest, ValidLoopbackDeviceTeardown) {
  EXPECT_TRUE(backing_device_->Setup());
  EXPECT_TRUE(backing_device_->Teardown());

  EXPECT_EQ(backing_device_->GetPath(), base::nullopt);
}

}  // namespace cryptohome
