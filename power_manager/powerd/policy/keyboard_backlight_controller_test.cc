// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/keyboard_backlight_controller.h"

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/backlight_controller_observer_stub.h"
#include "power_manager/powerd/policy/backlight_controller_stub.h"
#include "power_manager/powerd/system/ambient_light_sensor_stub.h"
#include "power_manager/powerd/system/backlight_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/proto_bindings/policy.pb.h"

namespace power_manager {
namespace policy {

class KeyboardBacklightControllerTest : public ::testing::Test {
 public:
  KeyboardBacklightControllerTest()
      : max_backlight_level_(100),
        initial_backlight_level_(50),
        pass_light_sensor_(true),
        initial_als_lux_(0),
        initial_tablet_mode_(TabletMode::UNSUPPORTED),
        als_steps_pref_("20.0 -1 50\n50.0 35 75\n75.0 60 -1"),
        user_steps_pref_("0.0\n10.0\n40.0\n60.0\n100.0"),
        no_als_brightness_pref_(40.0),
        detect_hover_pref_(0),
        turn_on_for_user_activity_pref_(0),
        keep_on_ms_pref_(0),
        keep_on_during_video_ms_pref_(0),
        backlight_(max_backlight_level_,
                   initial_backlight_level_,
                   system::BacklightInterface::BrightnessScale::kUnknown),
        light_sensor_(initial_als_lux_),
        test_api_(&controller_) {
    test_api_.clock()->set_current_time_for_testing(
        base::TimeTicks::FromInternalValue(1000));
    controller_.AddObserver(&observer_);
  }

  ~KeyboardBacklightControllerTest() override {
    controller_.RemoveObserver(&observer_);
  }

  // Initializes |controller_|.
  virtual void Init() {
    backlight_.set_max_level(max_backlight_level_);
    backlight_.set_current_level(initial_backlight_level_);
    light_sensor_.set_lux(initial_als_lux_);

    prefs_.SetString(kKeyboardBacklightAlsStepsPref, als_steps_pref_);
    prefs_.SetString(kKeyboardBacklightUserStepsPref, user_steps_pref_);
    prefs_.SetDouble(kKeyboardBacklightNoAlsBrightnessPref,
                     no_als_brightness_pref_);
    prefs_.SetDouble(kAlsSmoothingConstantPref, 1.0);
    prefs_.SetInt64(kDetectHoverPref, detect_hover_pref_);
    prefs_.SetInt64(kKeyboardBacklightTurnOnForUserActivityPref,
                    turn_on_for_user_activity_pref_);
    prefs_.SetInt64(kKeyboardBacklightKeepOnMsPref, keep_on_ms_pref_);
    prefs_.SetInt64(kKeyboardBacklightKeepOnDuringVideoMsPref,
                    keep_on_during_video_ms_pref_);

    controller_.Init(&backlight_, &prefs_,
                     pass_light_sensor_ ? &light_sensor_ : nullptr,
                     &dbus_wrapper_, &display_backlight_controller_,
                     initial_lid_state_, initial_tablet_mode_);
  }

 protected:
  // Returns the hardware-specific brightness level that should be used when the
  // display is dimmed.
  int64_t GetDimmedLevel() {
    return static_cast<int64_t>(lround(
        KeyboardBacklightController::kDimPercent / 100 * max_backlight_level_));
  }

  // Advances |controller_|'s clock by |interval|.
  void AdvanceTime(const base::TimeDelta& interval) {
    test_api_.clock()->set_current_time_for_testing(
        test_api_.clock()->GetCurrentTime() + interval);
  }

  // Calls the IncreaseKeyboardBrightness D-Bus method.
  void CallIncreaseKeyboardBrightness() {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kIncreaseKeyboardBrightnessMethod);
    ASSERT_TRUE(dbus_wrapper_.CallExportedMethodSync(&method_call));
  }

  // Calls the DecreaseKeyboardBrightness D-Bus method.
  void CallDecreaseKeyboardBrightness() {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kDecreaseKeyboardBrightnessMethod);
    ASSERT_TRUE(dbus_wrapper_.CallExportedMethodSync(&method_call));
  }

  // Calls the GetKeyboardBrightnessPercent D-Bus method and returns the
  // percentage from the reply. Adds a failure and returns 0 on error.
  double CallGetKeyboardBrightnessPercent() {
    dbus::MethodCall method_call(kPowerManagerInterface,
                                 kGetKeyboardBrightnessPercentMethod);
    std::unique_ptr<dbus::Response> response =
        dbus_wrapper_.CallExportedMethodSync(&method_call);
    if (!response) {
      ADD_FAILURE() << kGetKeyboardBrightnessPercentMethod << " call failed";
      return 0.0;
    }

    double percent = 0.0;
    if (!dbus::MessageReader(response.get()).PopDouble(&percent))
      ADD_FAILURE() << "Bad " << kGetKeyboardBrightnessPercentMethod << " arg";
    return percent;
  }

  BacklightControllerStub display_backlight_controller_;

  // Max and initial brightness levels for |backlight_|.
  int64_t max_backlight_level_;
  int64_t initial_backlight_level_;

  // Should |light_sensor_| be passed to |controller_|?
  bool pass_light_sensor_;

  // Initial lux level reported by |light_sensor_|.
  int initial_als_lux_;

  // Initial  lid state and tablet mode passed to |controller_|.
  LidState initial_lid_state_ = LidState::NOT_PRESENT;
  TabletMode initial_tablet_mode_;

  // Values for various preferences.  These can be changed by tests before
  // Init() is called.
  std::string als_steps_pref_;
  std::string user_steps_pref_;
  double no_als_brightness_pref_;
  int64_t detect_hover_pref_;
  int64_t turn_on_for_user_activity_pref_;
  int64_t keep_on_ms_pref_;
  int64_t keep_on_during_video_ms_pref_;

  FakePrefs prefs_;
  system::BacklightStub backlight_;
  system::AmbientLightSensorStub light_sensor_;
  system::DBusWrapperStub dbus_wrapper_;
  BacklightControllerObserverStub observer_;
  KeyboardBacklightController controller_;
  KeyboardBacklightController::TestApi test_api_;
};

TEST_F(KeyboardBacklightControllerTest, GetBrightnessPercent) {
  Init();

  // GetKeyboardBrightnessPercent should initially return the backlight's
  // starting level.  (It's safe to compare levels and percents since we're
  // using a [0, 100] range to make things simpler.)
  EXPECT_DOUBLE_EQ(static_cast<double>(initial_backlight_level_),
                   CallGetKeyboardBrightnessPercent());

  // After increasing the brightness, the new level should be returned.
  CallIncreaseKeyboardBrightness();
  EXPECT_DOUBLE_EQ(static_cast<double>(backlight_.current_level()),
                   CallGetKeyboardBrightnessPercent());
}

TEST_F(KeyboardBacklightControllerTest, GetBrightnessPercentWithScaling) {
  user_steps_pref_ = "0.0\n5.0\n20.0\n30.0\n50.0";
  initial_backlight_level_ = 0;
  Init();

  // Level              0    5   20   30    50
  // Raw percentages    0.0  5.0 20.0 30.0  50.0
  // Scaled percentages 0.0 10.0 40.0 60.0 100.0
  std::vector<double> scaled_percents{0.0, 10.0, 40.0, 60.0, 100.0};
  std::vector<int> levels{0, 5, 20, 30, 50};

  for (size_t i = 0; i < levels.size(); i++) {
    EXPECT_DOUBLE_EQ(scaled_percents[i], CallGetKeyboardBrightnessPercent());
    EXPECT_EQ(levels[i], backlight_.current_level());
    CallIncreaseKeyboardBrightness();
  }
}

TEST_F(KeyboardBacklightControllerTest, TurnOffForFullscreenVideo) {
  als_steps_pref_ = "20.0 -1 50\n50.0 35 75\n75.0 60 -1";
  user_steps_pref_ = "0.0\n100.0";
  Init();
  controller_.HandleSessionStateChange(SessionState::STARTED);
  light_sensor_.NotifyObservers();
  ASSERT_EQ(20, backlight_.current_level());

  // Non-fullscreen video shouldn't turn off the backlight.
  controller_.HandleVideoActivity(false);
  EXPECT_EQ(20, backlight_.current_level());

  // Fullscreen video should turn it off.
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // If the video switches to non-fullscreen, the backlight should be turned on.
  controller_.HandleVideoActivity(false);
  EXPECT_EQ(20, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Let fullscreen video turn it off again.
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // If the timeout fires to indicate that video has stopped, the backlight
  // should be turned on.
  ASSERT_TRUE(test_api_.TriggerVideoTimeout());
  EXPECT_EQ(20, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Fullscreen video should be ignored when the user isn't logged in.
  controller_.HandleSessionStateChange(SessionState::STOPPED);
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(20, backlight_.current_level());

  // It should also be ignored after the brightness has been set by the user.
  controller_.HandleSessionStateChange(SessionState::STARTED);
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(0, backlight_.current_level());
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(100, backlight_.current_level());
  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(0, backlight_.current_level());
  ASSERT_TRUE(test_api_.TriggerVideoTimeout());
  EXPECT_EQ(0, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, OnAmbientLightUpdated) {
  initial_backlight_level_ = 20;
  als_steps_pref_ = "20.0 -1 50\n50.0 35 75\n75.0 60 -1";
  Init();
  ASSERT_EQ(20, backlight_.current_level());
  EXPECT_EQ(0, controller_.GetNumAmbientLightSensorAdjustments());

  // ALS returns bad value.
  light_sensor_.set_lux(-1);
  light_sensor_.NotifyObservers();
  EXPECT_EQ(20, backlight_.current_level());

  // ALS returns a value in the middle of the initial step.
  light_sensor_.set_lux(25);
  light_sensor_.NotifyObservers();
  EXPECT_EQ(20, backlight_.current_level());

  // First increase; hysteresis not overcome.
  light_sensor_.set_lux(80);
  light_sensor_.NotifyObservers();
  EXPECT_EQ(20, backlight_.current_level());

  // Second increase; hysteresis overcome.  The lux is high enough that the
  // controller should skip over the middle step and use the top step.
  light_sensor_.NotifyObservers();
  EXPECT_EQ(75, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  EXPECT_EQ(1, controller_.GetNumAmbientLightSensorAdjustments());

  // First decrease; hysteresis not overcome.
  light_sensor_.set_lux(50);
  light_sensor_.NotifyObservers();
  EXPECT_EQ(75, backlight_.current_level());

  // Second decrease; hysteresis overcome.  The controller should decrease
  // to the middle step.
  light_sensor_.NotifyObservers();
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  EXPECT_EQ(2, controller_.GetNumAmbientLightSensorAdjustments());

  // The count should be reset after a new session starts.
  controller_.HandleSessionStateChange(SessionState::STARTED);
  EXPECT_EQ(0, controller_.GetNumAmbientLightSensorAdjustments());
}

TEST_F(KeyboardBacklightControllerTest, ChangeStates) {
  // Configure a single step for ALS and three steps for user control.
  als_steps_pref_ = "50.0 -1 -1";
  user_steps_pref_ = "0.0\n10.0\n100.0";
  initial_backlight_level_ = 50;
  Init();
  light_sensor_.NotifyObservers();
  ASSERT_EQ(50, backlight_.current_level());

  // Requests to dim the backlight and turn it off should be honored, as
  // should changes to turn it back on and undim.
  controller_.SetDimmedForInactivity(true);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetOffForInactivity(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetOffForInactivity(false);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetDimmedForInactivity(false);
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Send an increase request to switch to user control.
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Go through the same sequence of state changes and check that the
  // user-control dimming level is used.
  controller_.SetDimmedForInactivity(true);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetOffForInactivity(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetOffForInactivity(false);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.SetDimmedForInactivity(false);
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, DontBrightenToDim) {
  // Set the bottom ALS step to 2%.
  als_steps_pref_ = "2.0 -1 60\n80.0 40 -1";
  initial_als_lux_ = 2;
  Init();
  ASSERT_LT(initial_als_lux_, GetDimmedLevel());

  light_sensor_.NotifyObservers();
  ASSERT_EQ(initial_als_lux_, backlight_.current_level());

  // The controller should never increase the brightness level when dimming.
  controller_.SetDimmedForInactivity(true);
  EXPECT_EQ(initial_als_lux_, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, DeferChangesWhileDimmed) {
  als_steps_pref_ = "20.0 -1 60\n80.0 40 -1";
  initial_als_lux_ = 20;
  Init();

  light_sensor_.NotifyObservers();
  ASSERT_EQ(initial_als_lux_, backlight_.current_level());

  controller_.SetDimmedForInactivity(true);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());

  // ALS-driven changes shouldn't be applied while the screen is dimmed.
  light_sensor_.set_lux(80);
  light_sensor_.NotifyObservers();
  light_sensor_.NotifyObservers();
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());

  // The new ALS level should be used immediately after undimming, though.
  controller_.SetDimmedForInactivity(false);
  EXPECT_EQ(80, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, InitialUserLevelDownFirst) {
  // Set user steps at 0, 10, 40, 60, and 100.  The backlight should remain
  // at its starting level when Init() is called.
  user_steps_pref_ = "0.0\n10.0\n40.0\n60.0\n100.0";
  initial_backlight_level_ = 15;
  Init();
  EXPECT_EQ(15, backlight_.current_level());

  // After an increase request switches to user control of the brightness
  // level, the controller should first choose the step (10) nearest to the
  // initial level (15) and then increase to the next step (40).
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(40, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, InitialUserLevelUpFirst) {
  // Set user steps at 0, 10, 40, 60, and 100.  The backlight should remain
  // at its starting level when Init() is called.
  user_steps_pref_ = "0.0\n10.0\n40.0\n60.0\n100.0";
  initial_backlight_level_ = 30;
  Init();
  EXPECT_EQ(30, backlight_.current_level());

  // After an increase request switches to user control of the brightness
  // level, the controller should first choose the step (40) nearest to the
  // initial level (30) and then increase to the next step (60).
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(60, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, InitialAlsLevel) {
  // Set an initial backlight level that's closest to the 60% step and
  // within its lux range of [50, 90].
  als_steps_pref_ = "0.0 -1 30\n30.0 20 60\n60.0 50 90\n100.0 80 -1";
  initial_backlight_level_ = 55;
  initial_als_lux_ = 85;
  Init();
  EXPECT_EQ(55, backlight_.current_level());

  // After an ambient light reading, the controller should slowly
  // transition to the 60% level.
  light_sensor_.NotifyObservers();
  EXPECT_EQ(60, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, IncreaseBrightness) {
  user_steps_pref_ = "0.0\n10.0\n40.0\n60.0\n100.0";
  initial_backlight_level_ = 0;
  Init();

  EXPECT_EQ(0, backlight_.current_level());

  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(10, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  EXPECT_EQ(1, controller_.GetNumUserAdjustments());

  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(40, backlight_.current_level());
  EXPECT_EQ(2, controller_.GetNumUserAdjustments());

  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(60, backlight_.current_level());
  EXPECT_EQ(3, controller_.GetNumUserAdjustments());

  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(4, controller_.GetNumUserAdjustments());

  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(5, controller_.GetNumUserAdjustments());

  // The count should be reset after a new session starts.
  controller_.HandleSessionStateChange(SessionState::STARTED);
  EXPECT_EQ(0, controller_.GetNumUserAdjustments());
}

TEST_F(KeyboardBacklightControllerTest, DecreaseBrightness) {
  user_steps_pref_ = "0.0\n10.0\n40.0\n60.0\n100.0";
  initial_backlight_level_ = 100;
  Init();

  EXPECT_EQ(100, backlight_.current_level());

  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(60, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  EXPECT_EQ(1, controller_.GetNumUserAdjustments());

  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(40, backlight_.current_level());
  EXPECT_EQ(2, controller_.GetNumUserAdjustments());

  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(10, backlight_.current_level());
  EXPECT_EQ(3, controller_.GetNumUserAdjustments());

  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(4, controller_.GetNumUserAdjustments());

  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(5, controller_.GetNumUserAdjustments());
}

TEST_F(KeyboardBacklightControllerTest, TurnOffWhenSuspended) {
  initial_backlight_level_ = 50;
  no_als_brightness_pref_ = 50;
  pass_light_sensor_ = false;
  Init();
  controller_.SetSuspended(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());

  controller_.SetSuspended(false);
  EXPECT_EQ(50, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, TurnOffWhenShuttingDown) {
  Init();
  controller_.SetShuttingDown(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, TurnOffWhenLidClosed) {
  initial_lid_state_ = LidState::OPEN;
  Init();
  ASSERT_EQ(initial_backlight_level_, backlight_.current_level());

  controller_.HandleLidStateChange(LidState::CLOSED);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());

  // User requests to increase the brightness shouldn't turn the backlight on.
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(0, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, TurnOffWhenDisplayBacklightIsOff) {
  als_steps_pref_ = "50.0 -1 -1";
  user_steps_pref_ = "0.0\n100.0";
  initial_backlight_level_ = 50;
  Init();
  light_sensor_.set_lux(100);
  light_sensor_.NotifyObservers();

  display_backlight_controller_.NotifyObservers(
      10.0, BacklightBrightnessChange_Cause_USER_REQUEST);
  EXPECT_EQ(50, backlight_.current_level());

  // When the display backlight's brightness goes to zero while the
  // keyboard backlight is using an ambient-light-derived brightness, the
  // keyboard backlight should be turned off automatically.
  display_backlight_controller_.NotifyObservers(
      0.0, BacklightBrightnessChange_Cause_USER_REQUEST);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  display_backlight_controller_.NotifyObservers(
      20.0, BacklightBrightnessChange_Cause_USER_REQUEST);
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // After switching to user control of the brightness, the keyboard
  // backlight shouldn't be turned off automatically.
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  display_backlight_controller_.NotifyObservers(
      0.0, BacklightBrightnessChange_Cause_USER_REQUEST);
  EXPECT_EQ(100, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, Hover) {
  als_steps_pref_ = "50.0 -1 -1";
  user_steps_pref_ = "0.0\n100.0";
  detect_hover_pref_ = 1;
  keep_on_ms_pref_ = 30000;
  keep_on_during_video_ms_pref_ = 3000;
  Init();
  controller_.HandleSessionStateChange(SessionState::STARTED);
  light_sensor_.NotifyObservers();

  // The backlight should initially be off since the user isn't hovering.
  EXPECT_EQ(0, backlight_.current_level());

  // If hovering is detected, the backlight should be turned on quickly.
  controller_.HandleHoverStateChange(true);
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // It should remain on despite fullscreen video if hovering continues.
  controller_.HandleVideoActivity(true);
  EXPECT_EQ(50, backlight_.current_level());

  // It should remain on for a short period of time if hovering stops while the
  // video is still playing.
  controller_.HandleHoverStateChange(false);
  EXPECT_EQ(50, backlight_.current_level());

  // After enough time, the backlight should turn off.
  AdvanceTime(base::TimeDelta::FromMilliseconds(keep_on_during_video_ms_pref_));
  ASSERT_TRUE(test_api_.TriggerTurnOffTimeout());
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Stop the video. Since the user was hovering recently, the backlight should
  // turn back on.
  ASSERT_TRUE(test_api_.TriggerVideoTimeout());
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // After the rest of the full timeout, the backlight should turn off slowly.
  AdvanceTime(base::TimeDelta::FromMilliseconds(keep_on_ms_pref_ -
                                                keep_on_during_video_ms_pref_));
  ASSERT_TRUE(test_api_.TriggerTurnOffTimeout());
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // User activity should also turn the keyboard backlight on for the full
  // delay.
  controller_.HandleUserActivity(USER_ACTIVITY_OTHER);
  EXPECT_EQ(50, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  AdvanceTime(base::TimeDelta::FromMilliseconds(keep_on_ms_pref_));
  ASSERT_TRUE(test_api_.TriggerTurnOffTimeout());
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Increase the brightness to 100, dim for inactivity, and check that hover
  // restores the user-requested level.
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  controller_.SetDimmedForInactivity(true);
  EXPECT_EQ(GetDimmedLevel(), backlight_.current_level());
  controller_.HandleHoverStateChange(true);
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // The backlight should stay on while hovering even if it's requested to turn
  // off for inactivity.
  controller_.SetOffForInactivity(true);
  EXPECT_EQ(100, backlight_.current_level());

  // Stop hovering and check that starting again turns the backlight on again.
  controller_.HandleHoverStateChange(false);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
  controller_.HandleHoverStateChange(true);
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // A notification that the system is shutting down should take precedence.
  controller_.SetShuttingDown(true);
  EXPECT_EQ(0, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, NoAmbientLightSensor) {
  initial_backlight_level_ = 0;
  no_als_brightness_pref_ = 40.0;
  user_steps_pref_ = "0.0\n10.0\n100.0";
  pass_light_sensor_ = false;
  Init();

  // The brightness should immediately transition to the level from the pref.
  EXPECT_EQ(40, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Subsequent adjustments should move between the user steps.
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(100, backlight_.current_level());
  CallDecreaseKeyboardBrightness();
  EXPECT_EQ(10, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, EnableForUserActivity) {
  initial_backlight_level_ = 50;
  no_als_brightness_pref_ = 40.0;
  user_steps_pref_ = "0.0\n100.0";
  turn_on_for_user_activity_pref_ = 1;
  keep_on_ms_pref_ = 30000;
  pass_light_sensor_ = false;
  Init();

  // The backlight should turn off initially.
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // User activity should result in the backlight being turned on quickly.
  controller_.HandleUserActivity(USER_ACTIVITY_OTHER);
  EXPECT_EQ(40, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Advance the time and report user activity again.
  AdvanceTime(base::TimeDelta::FromMilliseconds(keep_on_ms_pref_ / 2));
  controller_.HandleUserActivity(USER_ACTIVITY_OTHER);
  EXPECT_EQ(40, backlight_.current_level());

  // The backlight should be turned off |keep_on_ms_pref_| after the last report
  // of user activity.
  AdvanceTime(base::TimeDelta::FromMilliseconds(keep_on_ms_pref_));
  ASSERT_TRUE(test_api_.TriggerTurnOffTimeout());
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, PreemptTransitionForShutdown) {
  initial_backlight_level_ = 50;
  Init();

  // Notify the keyboard controller that the display has been turned off (as
  // happens when shutting down).
  display_backlight_controller_.NotifyObservers(
      0.0, BacklightBrightnessChange_Cause_OTHER);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Now notify the keyboard controller that the system is shutting down and
  // check that the previous transition is preempted in favor of turning off the
  // keyboard backlight immediately.
  backlight_.set_transition_in_progress(true);
  controller_.SetShuttingDown(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, TurnOffWhenInTabletMode) {
  // The backlight should be initially turned off if the device is already in
  // tablet mode.
  initial_backlight_level_ = 100;
  no_als_brightness_pref_ = 100.0;
  pass_light_sensor_ = false;
  initial_tablet_mode_ = TabletMode::ON;
  Init();
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kSlowBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // It should quickly turn on when the device leaves tablet mode.
  controller_.HandleTabletModeChange(TabletMode::OFF);
  EXPECT_EQ(100, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());

  // Going back to tablet mode should turn the backlight off again.
  controller_.HandleTabletModeChange(TabletMode::ON);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(kFastBacklightTransitionMs,
            backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, ForcedOff) {
  initial_backlight_level_ = 50;
  Init();
  ASSERT_GT(backlight_.current_level(), 0);

  controller_.SetForcedOff(true);
  EXPECT_EQ(0, backlight_.current_level());
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());

  controller_.SetForcedOff(false);
  EXPECT_GT(backlight_.current_level(), 0);
  EXPECT_EQ(0, backlight_.current_interval().InMilliseconds());
}

TEST_F(KeyboardBacklightControllerTest, ChangeBacklightDevice) {
  // Start out without a backlight device.
  user_steps_pref_ = "0.0\n50.0\n100.0";
  backlight_.set_device_exists(false);
  Init();
  CallIncreaseKeyboardBrightness();
  controller_.SetOffForInactivity(true);

  // Connect a device and check that the earlier off state is applied to it.
  backlight_.set_device_exists(true);
  backlight_.NotifyDeviceChanged();
  EXPECT_EQ(0, backlight_.current_level());
  controller_.SetOffForInactivity(false);
  CallIncreaseKeyboardBrightness();
  CallIncreaseKeyboardBrightness();
  EXPECT_EQ(max_backlight_level_, backlight_.current_level());

  // Disconnect the device and check that decrease requests are ignored.
  backlight_.set_device_exists(false);
  backlight_.NotifyDeviceChanged();
  CallDecreaseKeyboardBrightness();

  // The previous 100% brightness should be reapplied to a new device with a
  // different range.
  backlight_.set_device_exists(true);
  backlight_.set_max_level(200);
  backlight_.set_current_level(100);
  backlight_.NotifyDeviceChanged();
  EXPECT_EQ(200, backlight_.current_level());
}

TEST_F(KeyboardBacklightControllerTest, EmptyUserSteps) {
  user_steps_pref_ = "";
  EXPECT_DEATH(
      Init(),
      "No user brightness steps defined in keyboard_backlight_user_steps");
}

TEST_F(KeyboardBacklightControllerTest, UserStepsNotStartAt0) {
  user_steps_pref_ = "10.0\n50.0\n100.0";
  EXPECT_DEATH(
      Init(),
      "keyboard_backlight_user_steps starts at 10.000000 instead of 0.0");
}

TEST_F(KeyboardBacklightControllerTest, UserStepsTooBig) {
  user_steps_pref_ = "0.0\n50.0\n110.0";
  EXPECT_DEATH(Init(),
               "keyboard_backlight_user_steps step 110.000000 is outside "
               "\\[0.0, 100.0]");
}

TEST_F(KeyboardBacklightControllerTest, UserStepsTooSmall) {
  user_steps_pref_ = "0.0\n-10.0\n100.0";
  EXPECT_DEATH(Init(),
               "keyboard_backlight_user_steps step -10.000000 is outside "
               "\\[0.0, 100.0]");
}

TEST_F(KeyboardBacklightControllerTest, UserStepsNotStrictlyIncreasing) {
  user_steps_pref_ = "0.0\n0.0\n100.0";
  EXPECT_DEATH(Init(),
               "keyboard_backlight_user_steps is not strictly increasing");
}

}  // namespace policy
}  // namespace power_manager
