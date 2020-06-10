// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_MANAGER_INTERFACE_H_
#define HERMES_EUICC_MANAGER_INTERFACE_H_

#include "hermes/euicc_slot_info.h"

namespace hermes {

class EuiccManagerInterface {
 public:
  // Add/update an Euicc with the given EuiccInfo. Redundant updates will be
  // ignored. Calling this on a |physical_slot| without an existing Euicc will
  // lead to the creation of one.
  virtual void OnEuiccUpdated(uint8_t physical_slot,
                              EuiccSlotInfo slot_info) = 0;
  // Remove the Euicc at the corresponding physical slot. Calling this when no
  // Euicc exists for |physical_slot| is a noop.
  virtual void OnEuiccRemoved(uint8_t physical_slot) = 0;
};

}  // namespace hermes

#endif  // HERMES_EUICC_MANAGER_INTERFACE_H_
