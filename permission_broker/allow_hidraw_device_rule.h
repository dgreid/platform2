// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_ALLOW_HIDRAW_DEVICE_RULE_H_
#define PERMISSION_BROKER_ALLOW_HIDRAW_DEVICE_RULE_H_

#include "permission_broker/hidraw_subsystem_udev_rule.h"

namespace permission_broker {

// AllowHidrawDeviceRule encapsulates the policy that hidraw devices are allowed
// to be accessed. Any path passed to it that is owned by a device on the hidraw
// subsystem is |ALLOW|ed. All other paths are ignored.
class AllowHidrawDeviceRule : public HidrawSubsystemUdevRule {
 public:
  AllowHidrawDeviceRule();
  ~AllowHidrawDeviceRule() override = default;

  Result ProcessHidrawDevice(struct udev_device* device) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(AllowHidrawDeviceRule);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_ALLOW_HIDRAW_DEVICE_RULE_H_
