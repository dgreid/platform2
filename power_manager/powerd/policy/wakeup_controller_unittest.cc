// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/wakeup_controller.h"

#include <base/strings/stringprintf.h>

#include "gtest/gtest.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/system/acpi_wakeup_helper_stub.h"
#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace policy {

namespace {
// An artificial syspath for tests.
const char kSyspath0[] = "/sys/devices/test/0";
};

class WakeupControllerTest : public ::testing::Test {
 public:
  WakeupControllerTest()
      : default_allow_docked_mode_(true),
        initial_lid_state_(LID_OPEN),
        initial_display_mode_(DISPLAY_NORMAL) {
  }

 protected:
  std::string GetSysattr(const std::string& syspath,
                         const std::string& sysattr) {
    std::string value;
    if (!udev_.GetSysattr(syspath, sysattr, &value))
      return "(error)";
    return value;
  }

  bool GetAcpiWakeup(const std::string& acpi_name) {
    bool value = false;
    if (!acpi_wakeup_helper_.GetWakeupEnabled(acpi_name, &value)) {
      ADD_FAILURE() << "Expected ACPI wakeup for " << acpi_name
                    << " to be defined";
    }
    return value;
  }

  void AddDeviceWithTags(const std::string& syspath,
                         const std::string& tags) {
    udev_.SetSysattr(syspath, WakeupController::kPowerWakeup,
                     WakeupController::kDisabled);
    udev_.TaggedDeviceChanged(syspath, tags);
  }

  void InitWakeupController() {
    prefs_.SetInt64(kAllowDockedModePref, default_allow_docked_mode_);
    wakeup_controller_.Init(&udev_, &acpi_wakeup_helper_,
                            initial_lid_state_, initial_display_mode_,
                            &prefs_);
  }

  system::UdevStub udev_;
  system::AcpiWakeupHelperStub acpi_wakeup_helper_;
  FakePrefs prefs_;

  bool default_allow_docked_mode_;

  LidState initial_lid_state_;
  DisplayMode initial_display_mode_;

  WakeupController wakeup_controller_;
};

TEST_F(WakeupControllerTest, ConfigureWakeupOnInit) {
  AddDeviceWithTags(kSyspath0, WakeupController::kTagWakeup);

  EXPECT_EQ(WakeupController::kDisabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
  InitWakeupController();
  EXPECT_EQ(WakeupController::kEnabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
  EXPECT_TRUE(GetAcpiWakeup(WakeupController::kTPAD));
  EXPECT_FALSE(GetAcpiWakeup(WakeupController::kTSCR));
}

TEST_F(WakeupControllerTest, ConfigureWakeupOnAdd) {
  InitWakeupController();

  // The device starts out with wakeup disabled, but should get configured by
  // WakeupController right away.
  AddDeviceWithTags(kSyspath0, WakeupController::kTagWakeup);
  EXPECT_EQ(WakeupController::kEnabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
}

TEST_F(WakeupControllerTest, DisableWakeupWhenClosed) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s %s",
                         WakeupController::kTagWakeup,
                         WakeupController::kTagWakeupOnlyWhenUsable,
                         WakeupController::kTagUsableWhenLaptop));
  InitWakeupController();

  // In laptop mode, wakeup should be enabled.
  EXPECT_EQ(WakeupController::kEnabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
  EXPECT_TRUE(GetAcpiWakeup("TPAD"));

  // When the lid is closed, wakeup should be disabled.
  wakeup_controller_.SetLidState(LID_CLOSED);
  EXPECT_EQ(WakeupController::kDisabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
  EXPECT_FALSE(GetAcpiWakeup(WakeupController::kTPAD));
}

TEST_F(WakeupControllerTest, PermanentlyDisableWakeup) {
  AddDeviceWithTags(kSyspath0,
                    base::StringPrintf("%s %s",
                         WakeupController::kTagWakeup,
                         WakeupController::kTagWakeupDisabled));

  // Simulate a device that has wakeup enabled initially.
  udev_.SetSysattr(kSyspath0, WakeupController::kPowerWakeup,
                   WakeupController::kEnabled);
  InitWakeupController();
  EXPECT_EQ(WakeupController::kDisabled,
            GetSysattr(kSyspath0, WakeupController::kPowerWakeup));
}

TEST_F(WakeupControllerTest, ConfigureInhibit) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s",
                         WakeupController::kTagInhibit,
                         WakeupController::kTagUsableWhenLaptop));
  InitWakeupController();

  // In laptop mode, inhibit should be off.
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is closed, inhibit should be on.
  wakeup_controller_.SetLidState(LID_CLOSED);
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is open, inhibit should be off again.
  wakeup_controller_.SetLidState(LID_OPEN);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));
}

TEST_F(WakeupControllerTest, InhibitDocking) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s %s",
                         WakeupController::kTagInhibit,
                         WakeupController::kTagUsableWhenLaptop,
                         WakeupController::kTagUsableWhenDocked));
  initial_display_mode_ = DISPLAY_PRESENTATION;
  InitWakeupController();

  // In laptop mode, inhibit should be off.
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is closed, inhibit should remain off.
  wakeup_controller_.SetLidState(LID_CLOSED);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is open, inhibit should still be off.
  wakeup_controller_.SetLidState(LID_OPEN);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));
}

TEST_F(WakeupControllerTest, InhibitDockingDisallowed) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s %s",
                         WakeupController::kTagInhibit,
                         WakeupController::kTagUsableWhenLaptop,
                         WakeupController::kTagUsableWhenDocked));
  default_allow_docked_mode_ = false;
  initial_display_mode_ = DISPLAY_PRESENTATION;
  InitWakeupController();

  // In laptop mode, inhibit should be off.
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is closed, inhibit should be on since docking is disallowed,
  // even though the device supports it.
  wakeup_controller_.SetLidState(LID_CLOSED);
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is open, inhibit should be off again.
  wakeup_controller_.SetLidState(LID_OPEN);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));
}

TEST_F(WakeupControllerTest, SetDisplayModeExternalInput) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s %s",
                         WakeupController::kTagInhibit,
                         WakeupController::kTagUsableWhenLaptop,
                         WakeupController::kTagUsableWhenDocked));
  initial_lid_state_ = LID_CLOSED;
  InitWakeupController();

  // When the lid is closed with no external display, external input devices
  // should be inhibited.
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When an external display is attached, device should be un-inhibited.
  wakeup_controller_.SetDisplayMode(DISPLAY_PRESENTATION);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When external display goes away, input should be inhibited again.
  wakeup_controller_.SetDisplayMode(DISPLAY_NORMAL);
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));
}

TEST_F(WakeupControllerTest, SetDisplayModeInternalInput) {
  AddDeviceWithTags(
      kSyspath0,
      base::StringPrintf("%s %s",
                         WakeupController::kTagInhibit,
                         WakeupController::kTagUsableWhenLaptop));
  InitWakeupController();

  // Devices that are only usable when in laptop mode should not be inhibited
  // while the lid is open.
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When an external display is attached, device should remain uninhibited.
  wakeup_controller_.SetDisplayMode(DISPLAY_PRESENTATION);
  EXPECT_EQ("0", GetSysattr(kSyspath0, WakeupController::kInhibited));

  // When the lid is closed, internal input should be inhibited regardless
  // of display mode.
  wakeup_controller_.SetLidState(LID_CLOSED);
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));

  wakeup_controller_.SetDisplayMode(DISPLAY_NORMAL);
  EXPECT_EQ("1", GetSysattr(kSyspath0, WakeupController::kInhibited));
}

}  // namespace policy
}  // namespace power_manager
