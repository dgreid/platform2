// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_STUB_H_
#define POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_STUB_H_

#include "power_manager/powerd/policy/bluetooth_controller.h"

namespace power_manager {
namespace policy {

class BluetoothControllerStub : public BluetoothControllerInterface {
 public:
  BluetoothControllerStub() {}
  ~BluetoothControllerStub() override {}

  const bool AllowWakeup() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(BluetoothControllerStub);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_BLUETOOTH_CONTROLLER_STUB_H_
