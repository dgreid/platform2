// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/boot/boot_slot.h"

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/boot/mock_boot_device.h"

namespace dlcservice {

class BootSlotTest : public testing::Test {
 public:
  BootSlotTest() {
    auto mock_boot_device = std::make_unique<MockBootDevice>();
    boot_device_ = mock_boot_device.get();
    boot_slot_ = std::make_unique<BootSlot>(std::move(mock_boot_device));
  }

 protected:
  MockBootDevice* boot_device_;
  std::unique_ptr<BootSlot> boot_slot_;
};

TEST_F(BootSlotTest, SplitPartitionNameTest) {
  std::string disk;
  int part_num;

  EXPECT_TRUE(boot_slot_->SplitPartitionName("/dev/sda3", &disk, &part_num));
  EXPECT_EQ("/dev/sda", disk);
  EXPECT_EQ(3, part_num);

  EXPECT_TRUE(boot_slot_->SplitPartitionName("/dev/sdp1234", &disk, &part_num));
  EXPECT_EQ("/dev/sdp", disk);
  EXPECT_EQ(1234, part_num);

  EXPECT_TRUE(
      boot_slot_->SplitPartitionName("/dev/mmcblk0p3", &disk, &part_num));
  EXPECT_EQ("/dev/mmcblk0", disk);
  EXPECT_EQ(3, part_num);

  EXPECT_TRUE(
      boot_slot_->SplitPartitionName("/dev/ubiblock3_2", &disk, &part_num));
  EXPECT_EQ("/dev/ubiblock", disk);
  EXPECT_EQ(3, part_num);

  EXPECT_TRUE(boot_slot_->SplitPartitionName("/dev/loop10", &disk, &part_num));
  EXPECT_EQ("/dev/loop", disk);
  EXPECT_EQ(10, part_num);

  EXPECT_TRUE(
      boot_slot_->SplitPartitionName("/dev/loop28p11", &disk, &part_num));
  EXPECT_EQ("/dev/loop28", disk);
  EXPECT_EQ(11, part_num);

  EXPECT_TRUE(
      boot_slot_->SplitPartitionName("/dev/loop10_0", &disk, &part_num));
  EXPECT_EQ("/dev/loop", disk);
  EXPECT_EQ(10, part_num);

  EXPECT_TRUE(
      boot_slot_->SplitPartitionName("/dev/loop28p11_0", &disk, &part_num));
  EXPECT_EQ("/dev/loop28", disk);
  EXPECT_EQ(11, part_num);

  EXPECT_TRUE(boot_slot_->SplitPartitionName("/dev/123", &disk, &part_num));
  EXPECT_EQ("/dev/", disk);
  EXPECT_EQ(123, part_num);

  EXPECT_FALSE(
      boot_slot_->SplitPartitionName("/dev/mmcblk0p", &disk, &part_num));
  EXPECT_FALSE(boot_slot_->SplitPartitionName("/dev/sda", &disk, &part_num));
  EXPECT_FALSE(
      boot_slot_->SplitPartitionName("/dev/foo/bar", &disk, &part_num));
  EXPECT_FALSE(boot_slot_->SplitPartitionName("/", &disk, &part_num));
  EXPECT_FALSE(boot_slot_->SplitPartitionName("", &disk, &part_num));
  EXPECT_FALSE(boot_slot_->SplitPartitionName("/dev/_100", &disk, &part_num));
}

TEST_F(BootSlotTest, GetCurrentSlotTest) {
  std::string boot_disk_name;
  BootSlot::Slot current_slot;

  EXPECT_CALL(*boot_device_, GetBootDevice())
      .WillOnce(testing::Return("/dev/sda3"))
      .WillOnce(testing::Return("/dev/sda5"))
      .WillOnce(testing::Return("/dev/sdb3"))
      .WillOnce(testing::Return("/dev/sda"))
      .WillOnce(testing::Return("/dev/sda3"));
  EXPECT_CALL(*boot_device_, IsRemovableDevice(testing::_))
      .WillOnce(testing::Return(false))
      .WillOnce(testing::Return(false))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true));

  // Boot from A slot.
  EXPECT_TRUE(boot_slot_->GetCurrentSlot(&boot_disk_name, &current_slot));
  EXPECT_EQ(boot_disk_name, "/dev/sda");
  EXPECT_EQ(current_slot, BootSlot::Slot::A);

  // Boot from B slot.
  EXPECT_TRUE(boot_slot_->GetCurrentSlot(&boot_disk_name, &current_slot));
  EXPECT_EQ(boot_disk_name, "/dev/sda");
  EXPECT_EQ(current_slot, BootSlot::Slot::B);

  // Boot from removable device.
  EXPECT_TRUE(boot_slot_->GetCurrentSlot(&boot_disk_name, &current_slot));
  EXPECT_EQ(boot_disk_name, "/dev/sdb");
  EXPECT_EQ(current_slot, BootSlot::Slot::A);

  // Boot from an invalid device.
  EXPECT_FALSE(boot_slot_->GetCurrentSlot(&boot_disk_name, &current_slot));

  // Boot from removable device.
  bool is_device_removable = false;
  EXPECT_TRUE(boot_slot_->GetCurrentSlot(&boot_disk_name, &current_slot,
                                         &is_device_removable));
  EXPECT_EQ(boot_disk_name, "/dev/sda");
  EXPECT_EQ(current_slot, BootSlot::Slot::A);
  EXPECT_TRUE(is_device_removable);
}

}  // namespace dlcservice
