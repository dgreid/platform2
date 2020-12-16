// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_LOGICAL_VOLUME_BACKING_DEVICE_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_LOGICAL_VOLUME_BACKING_DEVICE_H_

#include "cryptohome/storage/encrypted_container/backing_device.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/values.h>
#include <brillo/blkdev_utils/lvm.h>

namespace cryptohome {

// `LogicalVolumeBackingDevice` represents a thin volume backing device.
class LogicalVolumeBackingDevice : public BackingDevice {
 public:
  // `LogicalVolumeBackingDevice` are defined by the following config values:
  // - `name`: Name of the logical volume.
  // - `thinpool_name`: Name of thinpool on which the logical volume resides.
  // - `physical_volume`: Name of device on which the logical volume should be
  //                      set up.
  // - `size`: Size of thin logical volume.
  LogicalVolumeBackingDevice(const BackingDeviceConfig& config,
                             std::unique_ptr<brillo::LogicalVolumeManager> lvm);
  explicit LogicalVolumeBackingDevice(const BackingDeviceConfig& config);
  ~LogicalVolumeBackingDevice() = default;

  // Creates the thin logical volume.
  bool Create() override;

  // Removed the thin logical volume. The volume should not be in-use before
  // calling this function.
  bool Purge() override;

  // Activates the logical volume.
  bool Setup() override;

  // Deactivates the logical volume.
  bool Teardown() override;

  // Gets the device type for reporting.
  BackingDeviceType GetType() override {
    return BackingDeviceType::kLogicalVolumeBackingDevice;
  }

  // Gets path to the logical volume's block device.
  base::Optional<base::FilePath> GetPath() override;

 private:
  base::Optional<brillo::LogicalVolume> GetLogicalVolume();

  const std::string name_;
  const uint64_t size_;
  const base::FilePath physical_volume_;
  const std::string thinpool_name_;

  std::unique_ptr<brillo::LogicalVolumeManager> lvm_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_LOGICAL_VOLUME_BACKING_DEVICE_H_
