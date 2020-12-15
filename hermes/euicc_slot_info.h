// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_EUICC_SLOT_INFO_H_
#define HERMES_EUICC_SLOT_INFO_H_

#include <base/optional.h>
#include <string>
#include <utility>

namespace hermes {

// Information used to inform an EuiccManagerInterface about an eUICC slot, and
// to create & update Euicc instances.
class EuiccSlotInfo {
 public:
  explicit EuiccSlotInfo(std::string eid)
      : logical_slot_(base::nullopt), eid_(std::move(eid)) {}
  explicit EuiccSlotInfo(uint8_t logical_slot, std::string eid)
      : logical_slot_(logical_slot), eid_(std::move(eid)) {}

  void SetLogicalSlot(base::Optional<uint8_t> logical_slot) {
    logical_slot_ = std::move(logical_slot);
  }
  bool IsActive() const { return logical_slot_.has_value(); }
  const std::string& eid() const { return eid_; }
  uint8_t GetLogicalSlot() const {
    CHECK(IsActive());
    return logical_slot_.value();
  }
  bool operator==(const EuiccSlotInfo& rhs) const {
    return logical_slot_ == rhs.logical_slot_ && eid_ == rhs.eid_;
  }

 private:
  base::Optional<uint8_t> logical_slot_;
  std::string eid_;
};

}  // namespace hermes

#endif  // HERMES_EUICC_SLOT_INFO_H_
