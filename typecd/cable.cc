// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/cable.h"

#include <re2/re2.h>

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

constexpr char kSOPPrimeAltModeRegex[] = R"(port(\d+)-plug0.(\d+))";

}  // namespace

namespace typecd {

bool Cable::AddAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kSOPPrimeAltModeRegex,
                      &port, &index)) {
    LOG(ERROR) << "Couldn't parse alt mode index from syspath " << mode_syspath;
    return false;
  }

  if (IsAltModePresent(index)) {
    LOG(ERROR) << "Alt mode already registered for syspath " << mode_syspath;
    return false;
  }

  auto alt_mode = AltMode::CreateAltMode(mode_syspath);
  if (!alt_mode) {
    LOG(ERROR) << "Error creating alt mode for syspath " << mode_syspath;
    return false;
  }

  alt_modes_.emplace(index, std::move(alt_mode));
  LOG(INFO) << "Added SOP' alt mode for port " << port << " index " << index;

  return true;
}

void Cable::RemoveAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kSOPPrimeAltModeRegex,
                      &port, &index)) {
    LOG(ERROR) << "Couldn't parse alt mode index from syspath " << mode_syspath;
    return;
  }

  auto it = alt_modes_.find(index);
  if (it == alt_modes_.end()) {
    LOG(INFO) << "Trying to delete non-existent SOP' alt mode " << index;
    return;
  }

  alt_modes_.erase(it);

  LOG(INFO) << "Removed SOP' alt mode for port " << port << " index " << index;
}

bool Cable::IsAltModePresent(int index) {
  auto it = alt_modes_.find(index);
  if (it != alt_modes_.end()) {
    return true;
  }

  LOG(INFO) << "SOP' Alt mode not found at index " << index;
  return false;
}

AltMode* Cable::GetAltMode(int index) {
  if (!IsAltModePresent(index))
    return nullptr;

  return alt_modes_.find(index)->second.get();
}

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
