// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/bluetooth_controller_stub.h"

namespace power_manager {
namespace policy {

const bool BluetoothControllerStub::AllowWakeup() const {
  return false;
}

}  // namespace policy
}  // namespace power_manager
