// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_BOOT_BOOT_SLOT_H_
#define DLCSERVICE_BOOT_BOOT_SLOT_H_

#include <memory>
#include <string>

#include <base/gtest_prod_util.h>
#include <base/macros.h>

namespace dlcservice {

class BootDeviceInterface;

class BootSlot {
 public:
  enum class Slot : int {
    A = 0,
    B = 1,
  };

  explicit BootSlot(std::unique_ptr<BootDeviceInterface> boot_device);
  ~BootSlot();

  // Gets the partition slot the system is currently booted from. Returns true
  // if the operation returns valid results, otherwise returns false.
  // |boot_disk_name_out| returns the device path of the disk the system is
  // booted from. For example, "/dev/sda". |current_slot_out| returns the slot
  // the system is currently booted from. |is_removable_out| returns whether the
  // system is currently booted from a removable device or not.
  bool GetCurrentSlot(std::string* boot_disk_name_out,
                      BootSlot::Slot* current_slot_out,
                      bool* is_removable_out = nullptr) const;

  // Returns the string representation of |Slot|.
  static std::string ToString(Slot slot);

 private:
  FRIEND_TEST(BootSlotTest, SplitPartitionNameTest);

  // Splits the partition device name into the block device name and partition
  // number. For example, "/dev/sda3" will be split into {"/dev/sda", 3} and
  // "/dev/mmcblk0p2" into {"/dev/mmcblk0", 2}
  // Returns false when malformed device name is passed in.
  // If both output parameters are omitted (null), can be used
  // just to test the validity of the device name. Note that the function
  // simply checks if the device name looks like a valid device, no other
  // checks are performed (i.e. it doesn't check if the device actually exists).
  bool SplitPartitionName(std::string partition_name,
                          std::string* disk_name_out,
                          int* partition_num_out) const;

  std::unique_ptr<BootDeviceInterface> boot_device_;

  DISALLOW_COPY_AND_ASSIGN(BootSlot);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_BOOT_BOOT_SLOT_H_
