// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/backing_device.h"

#include <memory>

#include "cryptohome/storage/encrypted_container/loopback_device.h"
#include "cryptohome/platform.h"

namespace cryptohome {

// static
std::unique_ptr<BackingDevice> BackingDevice::Generate(
    const BackingDeviceConfig& config, Platform* platform) {
  switch (config.type) {
    case BackingDeviceType::kLoopbackDevice:
      return std::make_unique<LoopbackDevice>(
          config, platform, std::make_unique<brillo::LoopDeviceManager>());
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
