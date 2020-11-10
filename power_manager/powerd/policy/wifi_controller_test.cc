// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/wifi_controller.h"

#include <base/macros.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace policy {
namespace {

// Stub implementation of WifiController::Delegate for use by tests.
class TestWifiControllerDelegate : public WifiController::Delegate {
 public:
  TestWifiControllerDelegate() = default;
  TestWifiControllerDelegate(const TestWifiControllerDelegate&) = delete;
  TestWifiControllerDelegate& operator=(const TestWifiControllerDelegate&) =
      delete;

  ~TestWifiControllerDelegate() override = default;

  int num_set_calls() const { return num_set_calls_; }
  RadioTransmitPower last_transmit_power() const {
    return last_transmit_power_;
  }
  WifiRegDomain last_reg_domain() const { return last_reg_domain_; }

  // Resets stat members.
  void ResetStats() {
    num_set_calls_ = 0;
    last_transmit_power_ = RadioTransmitPower::UNSPECIFIED;
    last_reg_domain_ = WifiRegDomain::NONE;
  }

  // WifiController::Delegate:
  void SetWifiTransmitPower(RadioTransmitPower power,
                            WifiRegDomain domain) override {
    CHECK_NE(power, RadioTransmitPower::UNSPECIFIED);
    num_set_calls_++;
    last_transmit_power_ = power;
    last_reg_domain_ = domain;
  }

 private:
  // Number of times that SetWifiTransmitPower() has been called.
  int num_set_calls_ = 0;

  // Last power mode passed to SetWifiTransmitPower().
  RadioTransmitPower last_transmit_power_ = RadioTransmitPower::UNSPECIFIED;

  WifiRegDomain last_reg_domain_ = WifiRegDomain::NONE;
};

}  // namespace

class WifiControllerTest : public ::testing::Test {
 public:
  WifiControllerTest() = default;
  WifiControllerTest(const WifiControllerTest&) = delete;
  WifiControllerTest& operator=(const WifiControllerTest&) = delete;

  ~WifiControllerTest() override = default;

 protected:
  // Calls |controller_|'s Init() method.
  void Init(TabletMode tablet_mode) {
    prefs_.SetInt64(kSetWifiTransmitPowerForTabletModePref,
                    set_transmit_power_tablet_pref_value_);
    prefs_.SetInt64(kSetWifiTransmitPowerForProximityPref,
                    set_transmit_power_proximity_pref_value_);
    controller_.Init(&delegate_, &prefs_, &udev_, tablet_mode);
  }

  // Sends a udev event announcing that a wifi device has been added.
  void SendUdevEvent() {
    udev_.NotifySubsystemObservers(
        {{WifiController::kUdevSubsystem, WifiController::kUdevDevtype, "", ""},
         system::UdevEvent::Action::ADD});
  }

  // Initial value for kSetWifiTransmitPowerForTabletModePref.
  bool set_transmit_power_tablet_pref_value_ = true;

  // Initial value for kSetWifiTransmitPowerForProximityPref.
  bool set_transmit_power_proximity_pref_value_ = false;

  system::UdevStub udev_;
  FakePrefs prefs_;
  TestWifiControllerDelegate delegate_;
  WifiController controller_;
};

TEST_F(WifiControllerTest, SetTransmitPowerForInitialTabletMode) {
  Init(TabletMode::ON);
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  EXPECT_EQ(WifiRegDomain::NONE, delegate_.last_reg_domain());
}

TEST_F(WifiControllerTest, SetTransmitPowerForInitialClamshellMode) {
  Init(TabletMode::OFF);
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
  EXPECT_EQ(WifiRegDomain::NONE, delegate_.last_reg_domain());
}

TEST_F(WifiControllerTest, SetTransmitPowerForTabletModeChange) {
  Init(TabletMode::OFF);
  delegate_.ResetStats();

  controller_.HandleTabletModeChange(TabletMode::ON);
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());

  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(2, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());

  // Don't set the power if the tablet mode didn't change.
  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(2, delegate_.num_set_calls());
}

TEST_F(WifiControllerTest, SetTransmitPowerForDeviceAdded) {
  Init(TabletMode::ON);
  delegate_.ResetStats();

  // Attempt to set transmit power again when a wifi device is added.
  SendUdevEvent();
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());

  // Non-add events, or additions of non-wifi devices, shouldn't do anything.
  udev_.NotifySubsystemObservers(
      {{WifiController::kUdevSubsystem, WifiController::kUdevDevtype, "", ""},
       system::UdevEvent::Action::CHANGE});
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(WifiRegDomain::NONE, delegate_.last_reg_domain());
  udev_.NotifySubsystemObservers(
      {{WifiController::kUdevSubsystem, "eth", "", ""},
       system::UdevEvent::Action::ADD});
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(WifiRegDomain::NONE, delegate_.last_reg_domain());
}

TEST_F(WifiControllerTest, DontSetTransmitPowerWhenUnsupported) {
  // The delegate shouldn't be called if tablet mode is unsupported.
  Init(TabletMode::UNSUPPORTED);
  EXPECT_EQ(0, delegate_.num_set_calls());
  controller_.HandleTabletModeChange(TabletMode::UNSUPPORTED);
  EXPECT_EQ(0, delegate_.num_set_calls());
  SendUdevEvent();
  EXPECT_EQ(0, delegate_.num_set_calls());
}

TEST_F(WifiControllerTest, DontSetTransmitPowerWhenDisabled) {
  // The delegate should never be called when the pref is set to false.
  set_transmit_power_tablet_pref_value_ = false;
  Init(TabletMode::ON);
  EXPECT_EQ(0, delegate_.num_set_calls());
  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(0, delegate_.num_set_calls());
  SendUdevEvent();
  EXPECT_EQ(0, delegate_.num_set_calls());
}

TEST_F(WifiControllerTest, ProximitySensor) {
  set_transmit_power_proximity_pref_value_ = true;
  Init(TabletMode::UNSUPPORTED);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  controller_.HandleProximityChange(UserProximity::NEAR);
  EXPECT_EQ(1, delegate_.num_set_calls());
  controller_.HandleProximityChange(UserProximity::FAR);
  EXPECT_EQ(2, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
}

TEST_F(WifiControllerTest, IgnoreTabletEventIfProximity) {
  set_transmit_power_proximity_pref_value_ = true;
  Init(TabletMode::UNSUPPORTED);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
}

TEST_F(WifiControllerTest, SetRegDomainOnRegDomainEventIfTablet) {
  set_transmit_power_tablet_pref_value_ = true;
  Init(TabletMode::ON);
  EXPECT_EQ(1, delegate_.num_set_calls());
  controller_.HandleRegDomainChange(WifiRegDomain::FCC);
  EXPECT_EQ(WifiRegDomain::FCC, delegate_.last_reg_domain());
  EXPECT_EQ(2, delegate_.num_set_calls());
}

TEST_F(WifiControllerTest, SetRegDomainOnRegDomainEventIfProximity) {
  set_transmit_power_proximity_pref_value_ = true;
  Init(TabletMode::UNSUPPORTED);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  controller_.HandleRegDomainChange(WifiRegDomain::FCC);
  EXPECT_EQ(WifiRegDomain::FCC, delegate_.last_reg_domain());
  EXPECT_EQ(2, delegate_.num_set_calls());
}

TEST_F(WifiControllerTest, MaintainRegDomainOnTabletEvent) {
  set_transmit_power_tablet_pref_value_ = true;
  Init(TabletMode::ON);
  EXPECT_EQ(WifiRegDomain::NONE, delegate_.last_reg_domain());
  controller_.HandleRegDomainChange(WifiRegDomain::FCC);
  EXPECT_EQ(WifiRegDomain::FCC, delegate_.last_reg_domain());
  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(WifiRegDomain::FCC, delegate_.last_reg_domain());
}

TEST_F(WifiControllerTest, MaintainTabletModeOnRegDomainEvent) {
  set_transmit_power_tablet_pref_value_ = true;
  Init(TabletMode::ON);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  controller_.HandleRegDomainChange(WifiRegDomain::FCC);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
}

}  // namespace policy
}  // namespace power_manager
