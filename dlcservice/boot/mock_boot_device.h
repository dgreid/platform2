// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_BOOT_MOCK_BOOT_DEVICE_H_
#define DLCSERVICE_BOOT_MOCK_BOOT_DEVICE_H_

#include "dlcservice/boot/boot_device.h"

#include <string>

namespace dlcservice {

class MockBootDevice : public BootDeviceInterface {
 public:
  MockBootDevice() = default;

  MOCK_METHOD(bool, IsRemovableDevice, (const std::string&), (override));
  MOCK_METHOD(std::string, GetBootDevice, (), (override));

 private:
  MockBootDevice(const MockBootDevice&) = delete;
  MockBootDevice& operator=(const MockBootDevice&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_BOOT_MOCK_BOOT_DEVICE_H_
