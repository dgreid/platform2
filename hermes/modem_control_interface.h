// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_MODEM_CONTROL_INTERFACE_H_
#define HERMES_MODEM_CONTROL_INTERFACE_H_

namespace hermes {

class ModemControlInterface {
 public:
  // Stores the current active slot, and switches to physical_slot
  // Use this function to perform temporary slot switches
  virtual void StoreAndSetActiveSlot(uint32_t physical_slot) = 0;

  // Restore the value stored by StoreAndSetActiveSlot
  virtual void RestoreActiveSlot() = 0;

  virtual void StartProfileOp(uint32_t physical_slot) = 0;
  virtual void FinishProfileOp() = 0;

  virtual ~ModemControlInterface() = default;
};

}  // namespace hermes

#endif  // HERMES_MODEM_CONTROL_INTERFACE_H_
