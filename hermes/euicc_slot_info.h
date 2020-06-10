// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_SLOT_INFO_H_
#define HERMES_EUICC_SLOT_INFO_H_

#include <base/optional.h>

namespace hermes {

// Information used to inform an EuiccManagerInterface about an eUICC slot, and
// to create & update Euicc instances.
class EuiccSlotInfo {
 public:
  EuiccSlotInfo() : logical_slot_(base::nullopt) {}
  explicit EuiccSlotInfo(uint8_t logical_slot) : logical_slot_(logical_slot) {}

  bool IsActive() const { return logical_slot_.has_value(); }
  uint8_t GetLogicalSlot() const {
    CHECK(IsActive());
    return logical_slot_.value();
  }

 private:
  base::Optional<uint8_t> logical_slot_;
};

}  // namespace hermes

#endif  // HERMES_EUICC_SLOT_INFO_H_
