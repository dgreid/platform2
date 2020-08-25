// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_DENY_USB_VENDOR_ID_RULE_H_
#define PERMISSION_BROKER_DENY_USB_VENDOR_ID_RULE_H_

#include <stdint.h>

#include <string>

#include <base/macros.h>

#include "permission_broker/usb_subsystem_udev_rule.h"

namespace permission_broker {

class DenyUsbVendorIdRule : public UsbSubsystemUdevRule {
 public:
  explicit DenyUsbVendorIdRule(const uint16_t vendor_id);
  ~DenyUsbVendorIdRule() override = default;

  Result ProcessUsbDevice(struct udev_device* device) override;

 private:
  const std::string vendor_id_;

  DISALLOW_COPY_AND_ASSIGN(DenyUsbVendorIdRule);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_DENY_USB_VENDOR_ID_RULE_H_
