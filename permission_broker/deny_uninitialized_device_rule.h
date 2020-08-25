// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_DENY_UNINITIALIZED_DEVICE_RULE_H_
#define PERMISSION_BROKER_DENY_UNINITIALIZED_DEVICE_RULE_H_

#include "permission_broker/rule.h"

namespace permission_broker {

class DenyUninitializedDeviceRule : public Rule {
 public:
  DenyUninitializedDeviceRule();
  ~DenyUninitializedDeviceRule() override = default;

  Result ProcessDevice(struct udev_device* device) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(DenyUninitializedDeviceRule);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_DENY_UNINITIALIZED_DEVICE_RULE_H_
