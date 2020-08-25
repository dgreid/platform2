// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_DENY_CLAIMED_HIDRAW_DEVICE_RULE_H_
#define PERMISSION_BROKER_DENY_CLAIMED_HIDRAW_DEVICE_RULE_H_

#include "permission_broker/hidraw_subsystem_udev_rule.h"

namespace permission_broker {

// DenyClaimedHidrawDeviceRule encapsulates the policy that a HID device can
// only be accessed through the hidraw subsystem when no other device subsystems
// (apart from HID and USB themselves) are using the device.
class DenyClaimedHidrawDeviceRule : public HidrawSubsystemUdevRule {
 public:
  DenyClaimedHidrawDeviceRule();
  ~DenyClaimedHidrawDeviceRule() override = default;

  Result ProcessHidrawDevice(struct udev_device* device) override;

  // Indicates if a hidraw device should be inaccessible given the subsystem
  // identifier of one of its siblings.
  static bool ShouldSiblingSubsystemExcludeHidAccess(
      struct udev_device* sibling);

  static bool ShouldInputCapabilitiesExcludeHidAccess(
      const char* abs_capabilities,
      const char* rel_capabilities,
      const char* key_capabilities);

 private:
  DISALLOW_COPY_AND_ASSIGN(DenyClaimedHidrawDeviceRule);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_DENY_CLAIMED_HIDRAW_DEVICE_RULE_H_
