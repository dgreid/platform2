// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/boot/boot_slot.h"

#include <limits.h>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "dlcservice/boot/boot_device.h"
#include "dlcservice/utils.h"

using std::string;
using std::unique_ptr;

namespace dlcservice {

BootSlot::BootSlot(unique_ptr<BootDeviceInterface> boot_device)
    : boot_device_(std::move(boot_device)) {}

BootSlot::~BootSlot() {}

bool BootSlot::GetCurrentSlot(string* boot_disk_name_out,
                              Slot* current_slot_out) const {
  CHECK(boot_disk_name_out);
  CHECK(current_slot_out);

  string boot_device = boot_device_->GetBootDevice();
  if (boot_device.empty())
    return false;

  int partition_num;
  if (!SplitPartitionName(boot_device, boot_disk_name_out, &partition_num))
    return false;

  // All installed Chrome OS devices have two slots. We don't update removable
  // devices, so we will pretend we have only one slot in that case.
  if (boot_device_->IsRemovableDevice(*boot_disk_name_out))
    LOG(INFO)
        << "Booted from a removable device, pretending we have only one slot.";

  // Search through the slots to see which slot has the |partition_num| we
  // booted from.
  // In Chrome OS, the partition numbers are hard-coded:
  //   KERNEL-A=2, ROOT-A=3, KERNEL-B=4, ROOT-B=5, ...
  // To help compatibility between different casing we accept both lowercase and
  // uppercase names in the ChromeOS or Brillo standard names.
  // See http://www.chromium.org/chromium-os/chromiumos-design-docs/disk-format
  switch (partition_num) {
    case 2:  // KERNEL-A=2
    case 3:  // ROOT-A=2
      *current_slot_out = Slot::A;
      return true;
    case 4:  // KERNEL-B=4
    case 5:  // ROOT-B=5
      *current_slot_out = Slot::B;
      return true;
  }

  // This should map to one of the existing slots, otherwise something is very
  // wrong.
  LOG(ERROR) << "Couldn't find the slot number corresponding to the "
                "partition "
             << boot_device << ". This device is not updateable.";
  return false;
}

bool BootSlot::SplitPartitionName(string partition_name,
                                  string* disk_name_out,
                                  int* partition_num_out) const {
  CHECK(disk_name_out);
  CHECK(partition_num_out);
  if (!base::StartsWith(partition_name, "/dev/",
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Invalid partition device name: " << partition_name;
    return false;
  }

  // Loop twice if we hit the '_' case to handle NAND block devices.
  for (int i = 0; i <= 1; ++i) {
    auto nondigit_pos = partition_name.find_last_not_of("0123456789");
    if (!isdigit(partition_name.back()) || nondigit_pos == string::npos) {
      LOG(ERROR) << "Unable to parse partition device name: " << partition_name;
      return false;
    }

    switch (partition_name[nondigit_pos]) {
      // NAND block devices have weird naming which could be something like
      // "/dev/ubiblock2_0". We discard "_0" in such a case.
      case '_':
        LOG(INFO) << "Shortening partition_name: " << partition_name;
        partition_name = partition_name.substr(0, nondigit_pos);
        break;
      // Special case for MMC devices which have the following naming scheme:
      //   mmcblk0p2
      case 'p':
        if (nondigit_pos != 0 && isdigit(partition_name[nondigit_pos - 1])) {
          *disk_name_out = partition_name.substr(0, nondigit_pos);
          base::StringToInt(partition_name.substr(nondigit_pos + 1),
                            partition_num_out);
          return true;
        }
        FALLTHROUGH;
      default:
        *disk_name_out = partition_name.substr(0, nondigit_pos + 1);
        base::StringToInt(partition_name.substr(nondigit_pos + 1),
                          partition_num_out);
        return true;
    }
  }
  LOG(ERROR) << "Unable to parse partition device name: " << partition_name;
  return false;
}

// static
string BootSlot::ToString(BootSlot::Slot slot) {
  return slot == BootSlot::Slot::A ? kDlcDirAName : kDlcDirBName;
}

}  // namespace dlcservice
