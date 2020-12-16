// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/backing_device.h"

#include <memory>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/loopback_device.h"
#if USE_LVM_STATEFUL_PARTITION
#include "cryptohome/storage/encrypted_container/logical_volume_backing_device.h"
#endif  // USE_LVM_STATEFUL_PARTITION

namespace cryptohome {

// static
std::unique_ptr<BackingDevice> BackingDevice::Generate(
    const BackingDeviceConfig& config, Platform* platform) {
  switch (config.type) {
    case BackingDeviceType::kLoopbackDevice:
      return std::make_unique<LoopbackDevice>(
          config, platform, std::make_unique<brillo::LoopDeviceManager>());
#if USE_LVM_STATEFUL_PARTITION
    case BackingDeviceType::kLogicalVolumeBackingDevice:
      return std::make_unique<LogicalVolumeBackingDevice>(config);
#endif  // USE_LVM_STATEFUL_PARTITION
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
