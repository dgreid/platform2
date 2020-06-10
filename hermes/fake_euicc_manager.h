// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_FAKE_EUICC_MANAGER_H_
#define HERMES_FAKE_EUICC_MANAGER_H_

#include <map>
#include <utility>

#include "hermes/euicc_manager_interface.h"

namespace hermes {

class FakeEuiccManager : public EuiccManagerInterface {
 public:
  using SlotMap = std::map<uint8_t, EuiccSlotInfo>;

  const SlotMap& valid_slots() const { return valid_slots_; }

  // EuiccManagerInterface overrides.
  void OnEuiccUpdated(uint8_t physical_slot, EuiccSlotInfo slot_info) override {
    valid_slots_[physical_slot] = std::move(slot_info);
  }
  void OnEuiccRemoved(uint8_t physical_slot) override {
    valid_slots_.erase(physical_slot);
  }

 private:
  // Map of physical slot number -> eUICC slot info.
  SlotMap valid_slots_;
};

}  // namespace hermes

#endif  // HERMES_FAKE_EUICC_MANAGER_H_
