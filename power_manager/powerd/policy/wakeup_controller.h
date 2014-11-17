// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_WAKEUP_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_WAKEUP_CONTROLLER_H_

#include <string>

#include <base/basictypes.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/udev_tagged_device_observer.h"

namespace power_manager {
class PrefsInterface;

namespace system {
class AcpiWakeupHelperInterface;
class TaggedDevice;
class UdevInterface;
}  // namespace system

namespace policy {

// Describes which mode the system is currently in, depending on e.g. the state
// of the lid. Currently, WakeupController only tracks CLOSED and OPEN.
enum WakeupMode {
  WAKEUP_MODE_CLOSED = 0,  // Lid closed, no external monitor attached.
  WAKEUP_MODE_DOCKED,  // Lid closed, external monitor attached.
  WAKEUP_MODE_LAPTOP,  // Lid open.
  WAKEUP_MODE_TABLET,  // Tablet mode, e.g. lid open more than 180 degrees.
};

// Configures wakeup-capable devices according to the current lid state.
class WakeupController : public system::UdevTaggedDeviceObserver {
 public:
  // Powerd tags.
  static const char kTagInhibit[];
  static const char kTagUsableWhenDocked[];
  static const char kTagUsableWhenLaptop[];
  static const char kTagUsableWhenTablet[];
  static const char kTagWakeup[];
  static const char kTagWakeupOnlyWhenUsable[];
  static const char kTagWakeupDisabled[];

  // Sysfs power/wakeup constants.
  static const char kPowerWakeup[];
  static const char kEnabled[];
  static const char kDisabled[];
  static const char kUSBDevice[];

  static const char kInhibited[];

  // ACPI device names.
  static const char kTPAD[];
  static const char kTSCR[];

  WakeupController();
  virtual ~WakeupController();

  void Init(system::UdevInterface* udev,
            system::AcpiWakeupHelperInterface* acpi_wakeup_helper,
            LidState lid_state,
            DisplayMode display_mode,
            PrefsInterface* prefs);

  void SetLidState(LidState lid_state);
  void SetDisplayMode(DisplayMode display_mode);

  // Implementation of TaggedDeviceObserver.
  void OnTaggedDeviceChanged(const system::TaggedDevice& device) override;
  void OnTaggedDeviceRemoved(const system::TaggedDevice& device) override;

 private:
  // Derive the currently applicable WakeupMode according to lid state.
  WakeupMode GetWakeupMode() const;

  // Enables or disables wakeup from S3 for this device (through power/wakeup).
  void SetWakeupFromS3(const system::TaggedDevice& device, bool enabled);

  // Configures inhibit for |device| according to our policy.
  void ConfigureInhibit(const system::TaggedDevice& device);

  // Configures wakeup for |device| according to our policy.
  void ConfigureWakeup(const system::TaggedDevice& device);

  // Re-configures ACPI wakeup.
  void ConfigureAcpiWakeup();

  // Re-configures all known devices to reflect a policy change.
  void UpdatePolicy();

  system::UdevInterface* udev_;  // weak
  system::AcpiWakeupHelperInterface* acpi_wakeup_helper_;  // weak

  PrefsInterface* prefs_;  // weak

  LidState lid_state_;
  DisplayMode display_mode_;
  bool allow_docked_mode_;

  // The mode calculated in the most recent invocation of UpdatePolicy().
  WakeupMode mode_;

  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(WakeupController);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_WAKEUP_CONTROLLER_H_
