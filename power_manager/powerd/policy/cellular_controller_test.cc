// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/cellular_controller.h"

#include <base/macros.h>
#include <gtest/gtest.h>

#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace policy {
namespace {

constexpr int64_t kFakeDprGpioNumber = 123;
constexpr int64_t kUnknownDprGpioNumber = -1;
constexpr int64_t kInvalidDprGpioNumber = -2;

// Stub implementation of CellularController::Delegate for use by tests.
class TestCellularControllerDelegate : public CellularController::Delegate {
 public:
  TestCellularControllerDelegate() = default;
  TestCellularControllerDelegate(const TestCellularControllerDelegate&) =
      delete;
  TestCellularControllerDelegate& operator=(
      const TestCellularControllerDelegate&) = delete;

  ~TestCellularControllerDelegate() override = default;

  int num_set_calls() const { return num_set_calls_; }
  RadioTransmitPower last_transmit_power() const {
    return last_transmit_power_;
  }
  int64_t last_dpr_gpio_number() const { return last_dpr_gpio_number_; }

  // Resets stat members.
  void ResetStats() {
    num_set_calls_ = 0;
    last_transmit_power_ = RadioTransmitPower::UNSPECIFIED;
    last_dpr_gpio_number_ = kUnknownDprGpioNumber;
  }

  // CellularController::Delegate:
  void SetCellularTransmitPower(RadioTransmitPower power,
                                int64_t dpr_gpio_number) override {
    CHECK_NE(power, RadioTransmitPower::UNSPECIFIED);
    num_set_calls_++;
    last_transmit_power_ = power;
    last_dpr_gpio_number_ = dpr_gpio_number;
  }

 private:
  // Number of times that SetCellularTransmitPower() has been called.
  int num_set_calls_ = 0;

  // Last power mode passed to SetCellularTransmitPower().
  RadioTransmitPower last_transmit_power_ = RadioTransmitPower::UNSPECIFIED;

  // Last DPR GPIO number passed to SetCellularTransmitPower().
  int64_t last_dpr_gpio_number_ = kUnknownDprGpioNumber;
};

}  // namespace

class CellularControllerTest : public ::testing::Test {
 public:
  CellularControllerTest() = default;
  CellularControllerTest(const CellularControllerTest&) = delete;
  CellularControllerTest& operator=(const CellularControllerTest&) = delete;

  ~CellularControllerTest() override = default;

 protected:
  // Calls |controller_|'s Init() method.
  void Init(bool honor_proximity,
            bool honor_tablet_mode,
            int64_t dpr_gpio_number) {
    prefs_.SetInt64(kSetCellularTransmitPowerForProximityPref, honor_proximity);
    prefs_.SetInt64(kSetCellularTransmitPowerForTabletModePref,
                    honor_tablet_mode);
    if (dpr_gpio_number != kUnknownDprGpioNumber) {
      prefs_.SetInt64(kSetCellularTransmitPowerDprGpioPref, dpr_gpio_number);
    }
    controller_.Init(&delegate_, &prefs_);
  }

  FakePrefs prefs_;
  TestCellularControllerDelegate delegate_;
  CellularController controller_;
};

TEST_F(CellularControllerTest, LowPowerOnSensorDetect) {
  Init(true, false, kFakeDprGpioNumber);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  EXPECT_EQ(1, delegate_.num_set_calls());
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());
}

TEST_F(CellularControllerTest, PowerChangeOnProximityChange) {
  Init(true, false, kFakeDprGpioNumber);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());

  controller_.HandleProximityChange(UserProximity::FAR);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());

  controller_.HandleProximityChange(UserProximity::NEAR);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());
}

TEST_F(CellularControllerTest, ProximityIgnoredWhenOff) {
  Init(false, false, kFakeDprGpioNumber);
  controller_.ProximitySensorDetected(UserProximity::NEAR);
  EXPECT_EQ(0, delegate_.num_set_calls());

  controller_.HandleProximityChange(UserProximity::FAR);
  EXPECT_EQ(0, delegate_.num_set_calls());
}

TEST_F(CellularControllerTest, DprGpioNumberNotSpecified) {
  EXPECT_DEATH(Init(true, false, kUnknownDprGpioNumber), ".*");
}

TEST_F(CellularControllerTest, DprGpioNumberInvalid) {
  EXPECT_DEATH(Init(true, false, kInvalidDprGpioNumber), ".*");
}

TEST_F(CellularControllerTest, TabletMode) {
  Init(false, true, kFakeDprGpioNumber);

  controller_.HandleTabletModeChange(TabletMode::ON);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());

  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
  EXPECT_EQ(kFakeDprGpioNumber, delegate_.last_dpr_gpio_number());
}

TEST_F(CellularControllerTest, TabletModeIgnoredWhenOff) {
  Init(true, false, kFakeDprGpioNumber);
  controller_.ProximitySensorDetected(UserProximity::FAR);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());

  controller_.HandleTabletModeChange(TabletMode::ON);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
}

TEST_F(CellularControllerTest, ProximityAndTabletMode) {
  Init(true, true, kFakeDprGpioNumber);
  controller_.HandleTabletModeChange(TabletMode::ON);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());

  controller_.ProximitySensorDetected(UserProximity::FAR);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());

  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());

  controller_.HandleProximityChange(UserProximity::NEAR);
  EXPECT_EQ(RadioTransmitPower::LOW, delegate_.last_transmit_power());

  controller_.HandleProximityChange(UserProximity::FAR);
  EXPECT_EQ(RadioTransmitPower::HIGH, delegate_.last_transmit_power());
}

}  // namespace policy
}  // namespace power_manager
