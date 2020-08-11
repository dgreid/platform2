// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/cable.h"

namespace {

// Ref:
//   USB PD Spec rev 3.0, v2.0
//   Table 6-29: ID Header VDO
//   Table 6-38: Passive Cable VDO
constexpr uint32_t kActiveCableBitMask = (0x4 << 27);
constexpr uint32_t kPassiveCableBitMask = (0x3 << 27);

constexpr uint32_t kUSBSpeedBitMask = 0x3;
constexpr uint32_t kUSBSuperSpeed32Gen1 = 0x1;
constexpr uint32_t kUSBSuperSpeed32Or40Gen2 = 0x2;
constexpr uint32_t kUSB40SuperSpeedGen3 = 0x3;
// Speed values for PD rev 2.0
constexpr uint32_t kUSBSuperSpeed31Gen1 = 0x1;
constexpr uint32_t kUSBSuperSpeed31Gen2 = 0x2;

}  // namespace

namespace typecd {

// Ref:
//   USB Type-C Connector Spec, release 2.0
//   Figure F-1.
bool Cable::TBT3PDIdentityCheck() {
  // If the cable is active, we don't need to check for speed.
  if (GetIdHeaderVDO() & kActiveCableBitMask) {
    LOG(INFO) << "Active cable detected, TBT3 supported.";
    return true;
  }

  if (!(GetIdHeaderVDO() & kPassiveCableBitMask)) {
    LOG(ERROR) << "Cable has unsupported product type.";
    return false;
  }

  auto usb_speed = GetProductTypeVDO1() & kUSBSpeedBitMask;
  if (GetPDRevision() == kPDRevision30) {
    return usb_speed == kUSBSuperSpeed32Gen1 ||
           usb_speed == kUSBSuperSpeed32Or40Gen2 ||
           usb_speed == kUSB40SuperSpeedGen3;
  }

  // For PD 2.0 the check is similar, but let's make it explicit.
  return usb_speed == kUSBSuperSpeed31Gen1 || usb_speed == kUSBSuperSpeed31Gen2;
}

}  // namespace typecd
