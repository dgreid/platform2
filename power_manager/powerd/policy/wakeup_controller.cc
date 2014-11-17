// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/wakeup_controller.h"

#include <base/logging.h>
#include <vector>

#include "power_manager/common/prefs.h"
#include "power_manager/powerd/system/acpi_wakeup_helper.h"
#include "power_manager/powerd/system/tagged_device.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace policy {

namespace {

bool IsUsableInMode(const system::TaggedDevice& device, WakeupMode mode) {
  return ((device.HasTag(WakeupController::kTagUsableWhenDocked) &&
               mode == WAKEUP_MODE_DOCKED) ||
          (device.HasTag(WakeupController::kTagUsableWhenLaptop) &&
               mode == WAKEUP_MODE_LAPTOP) ||
          (device.HasTag(WakeupController::kTagUsableWhenTablet) &&
               mode == WAKEUP_MODE_TABLET));
}

}  // namespace

const char WakeupController::kTagInhibit[] = "inhibit";
const char WakeupController::kTagUsableWhenDocked[] = "usable_when_docked";
const char WakeupController::kTagUsableWhenLaptop[] = "usable_when_laptop";
const char WakeupController::kTagUsableWhenTablet[] = "usable_when_tablet";
const char WakeupController::kTagWakeup[] = "wakeup";
const char WakeupController::kTagWakeupOnlyWhenUsable[] =
    "wakeup_only_when_usable";
const char WakeupController::kTagWakeupDisabled[] = "wakeup_disabled";

const char WakeupController::kPowerWakeup[] = "power/wakeup";
const char WakeupController::kEnabled[] = "enabled";
const char WakeupController::kDisabled[] = "disabled";
const char WakeupController::kUSBDevice[] = "usb_device";

const char WakeupController::kInhibited[] = "inhibited";

const char WakeupController::kTPAD[] = "TPAD";
const char WakeupController::kTSCR[] = "TSCR";

WakeupController::WakeupController()
    : udev_(NULL),
      acpi_wakeup_helper_(NULL),
      prefs_(NULL),
      lid_state_(LID_OPEN),
      display_mode_(DISPLAY_NORMAL),
      allow_docked_mode_(false),
      mode_(WAKEUP_MODE_LAPTOP),
      initialized_(false) {}

WakeupController::~WakeupController() {
  if (udev_)
    udev_->RemoveTaggedDeviceObserver(this);
}

void WakeupController::Init(
    system::UdevInterface* udev,
    system::AcpiWakeupHelperInterface* acpi_wakeup_helper,
    LidState lid_state,
    DisplayMode display_mode,
    PrefsInterface* prefs) {
  udev_ = udev;
  acpi_wakeup_helper_ = acpi_wakeup_helper;

  udev_->AddTaggedDeviceObserver(this);

  // Trigger initial configuration.
  prefs_ = prefs;
  lid_state_ = lid_state;
  display_mode_ = display_mode;
  prefs_->GetBool(kAllowDockedModePref, &allow_docked_mode_);

  UpdatePolicy();

  initialized_ = true;
}

void WakeupController::SetLidState(LidState lid_state) {
  lid_state_ = lid_state;
  UpdatePolicy();
}

void WakeupController::SetDisplayMode(DisplayMode display_mode) {
  display_mode_ = display_mode;
  UpdatePolicy();
}

void WakeupController::OnTaggedDeviceChanged(
    const system::TaggedDevice& device) {
  ConfigureInhibit(device);
  ConfigureWakeup(device);
}

void WakeupController::OnTaggedDeviceRemoved(
    const system::TaggedDevice& device) {}

void WakeupController::SetWakeupFromS3(const system::TaggedDevice& device,
                                       bool enabled) {
  // For USB devices, the input device does not have a power/wakeup property
  // itself, but the corresponding USB device does. If the matching device does
  // not have a power/wakeup property, we thus fall back to the first ancestor
  // that has one. Conflicts should not arise, since real-world USB input
  // devices typically only expose one input interface anyway. However,
  // crawling up sysfs should only reach the first "usb_device" node, because
  // higher-level nodes include USB hubs, and enabling wakeups on those isn't
  // a good idea.
  std::string parent_syspath;
  if (!udev_->FindParentWithSysattr(device.syspath(), kPowerWakeup,
                                    kUSBDevice, &parent_syspath)) {
    LOG(WARNING) << "No " << kPowerWakeup << " sysattr available for "
                 << device.syspath();
    return;
  }
  VLOG(1) << (enabled ? "Enabling" : "Disabling") << " wakeup for "
          << device.syspath() << " through " << parent_syspath;
  udev_->SetSysattr(parent_syspath,
                    kPowerWakeup,
                    enabled ? kEnabled : kDisabled);
}

void WakeupController::ConfigureInhibit(
    const system::TaggedDevice& device) {
  // Should this device be inhibited when it is not usable?
  if (!device.HasTag(kTagInhibit))
    return;
  bool inhibit = !IsUsableInMode(device, mode_);
  VLOG(1) << (inhibit ? "Inhibiting " : "Un-inhibiting ") << device.syspath();
  udev_->SetSysattr(device.syspath(), kInhibited, inhibit ? "1" : "0");
}

void WakeupController::ConfigureWakeup(
    const system::TaggedDevice& device) {
  // Do we manage wakeup for this device?
  if (!device.HasTag(kTagWakeup))
    return;

  bool wakeup = true;
  if (device.HasTag(kTagWakeupDisabled))
    wakeup = false;
  else if (device.HasTag(kTagWakeupOnlyWhenUsable))
    wakeup = IsUsableInMode(device, mode_);

  SetWakeupFromS3(device, wakeup);
}

void WakeupController::ConfigureAcpiWakeup() {
  // On x86 systems, setting power/wakeup in sysfs is not enough, we also need
  // to go through /proc/acpi/wakeup.

  if (!acpi_wakeup_helper_->IsSupported())
    return;

  acpi_wakeup_helper_->SetWakeupEnabled(kTPAD, mode_ == WAKEUP_MODE_LAPTOP);
  acpi_wakeup_helper_->SetWakeupEnabled(kTSCR, false);
}

WakeupMode WakeupController::GetWakeupMode() const {
  if (allow_docked_mode_ && display_mode_ == DISPLAY_PRESENTATION &&
      lid_state_ == LID_CLOSED)
    return WAKEUP_MODE_DOCKED;

  if (lid_state_ == LID_OPEN)
    return WAKEUP_MODE_LAPTOP;
  else if (lid_state_ == LID_CLOSED)
    return WAKEUP_MODE_CLOSED;
  else
    return WAKEUP_MODE_LAPTOP;
}

void WakeupController::UpdatePolicy() {
  DCHECK(udev_);

  WakeupMode new_mode = GetWakeupMode();
  if (initialized_ && mode_ == new_mode)
    return;

  mode_ = new_mode;

  VLOG(1) << "Policy changed, re-configuring existing devices";

  std::vector<system::TaggedDevice> devices = udev_->GetTaggedDevices();
  // Configure inhibit first, as it is somewhat time-critical (we want to block
  // events as fast as possible), and wakeup takes a few milliseconds to set.
  for (const system::TaggedDevice& device : devices)
    ConfigureInhibit(device);
  for (const system::TaggedDevice& device : devices)
    ConfigureWakeup(device);

  ConfigureAcpiWakeup();
}

}  // namespace policy
}  // namespace power_manager
