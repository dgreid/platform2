// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_unsafe_hidraw_device_rule.h"

#include <libudev.h>

#include <vector>

namespace permission_broker {

namespace {

bool IsKeyboardUsage(const HidUsage& usage) {
  if (usage.page == HidUsage::PAGE_KEYBOARD)
    return true;

  if (usage.page == HidUsage::PAGE_GENERIC_DESKTOP) {
    return usage.usage == HidUsage::GENERIC_DESKTOP_USAGE_KEYBOARD ||
           usage.usage == HidUsage::GENERIC_DESKTOP_USAGE_KEYPAD;
  }

  return false;
}

bool IsPointerUsage(const HidUsage& usage) {
  if (usage.page == HidUsage::PAGE_GENERIC_DESKTOP) {
    return usage.usage == HidUsage::GENERIC_DESKTOP_USAGE_POINTER ||
           usage.usage == HidUsage::GENERIC_DESKTOP_USAGE_MOUSE;
  }
  return false;
}

bool IsSystemControlUsage(const HidUsage& usage) {
  if (usage.page != HidUsage::PAGE_GENERIC_DESKTOP)
    return false;
  if (usage.usage >= HidUsage::GENERIC_DESKTOP_USAGE_SYSTEM_CONTROL &&
      usage.usage <= HidUsage::GENERIC_DESKTOP_USAGE_SYSTEM_WARM_RESTART) {
    return true;
  }
  if (usage.usage >= HidUsage::GENERIC_DESKTOP_USAGE_SYSTEM_DOCK &&
      usage.usage <= HidUsage::GENERIC_DESKTOP_USAGE_SYSTEM_DISPLAY_SWAP) {
    return true;
  }
  return false;
}

}  // namespace

DenyUnsafeHidrawDeviceRule::DenyUnsafeHidrawDeviceRule()
    : HidrawSubsystemUdevRule("DenyUnsafeHidrawDeviceRule") {}

Rule::Result DenyUnsafeHidrawDeviceRule::ProcessHidrawDevice(
    struct udev_device* device) {
  std::vector<HidUsage> usages;
  if (!GetHidToplevelUsages(device, &usages)) {
    return IGNORE;
  }
  for (std::vector<HidUsage>::const_iterator iter = usages.begin();
       iter != usages.end(); ++iter) {
    if (IsUnsafeUsage(*iter)) {
      return DENY;
    }
  }
  return IGNORE;
}

// static
bool DenyUnsafeHidrawDeviceRule::IsUnsafeUsage(const HidUsage& usage) {
  return IsKeyboardUsage(usage) || IsPointerUsage(usage) ||
         IsSystemControlUsage(usage);
}

}  // namespace permission_broker
