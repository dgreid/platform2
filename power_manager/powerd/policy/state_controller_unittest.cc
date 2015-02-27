// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/state_controller.h"

#include <stdint.h>

#include <base/compiler_specific.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <gtest/gtest.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/clock.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/power_constants.h"

namespace power_manager {
namespace policy {

namespace {

// Strings returned by TestDelegate::GetActions() to describe various
// actions that were requested.
const char kScreenDim[] = "dim";
const char kScreenOff[] = "off";
const char kScreenLock[] = "lock";
const char kScreenUndim[] = "undim";
const char kScreenOn[] = "on";
const char kSuspend[] = "suspend";
const char kStopSession[] = "logout";
const char kShutDown[] = "shut_down";
const char kDocked[] = "docked";
const char kUndocked[] = "undocked";
const char kIdleDeferred[] = "idle_deferred";
const char kReportUserActivityMetrics[] = "metrics";

// String returned by TestDelegate::GetActions() if no actions were
// requested.
const char kNoActions[] = "";

std::string GetIdleImminentAction(base::TimeDelta time_until_idle_action) {
  return base::StringPrintf("idle_imminent(%" PRId64 ")",
                            time_until_idle_action.InMilliseconds());
}

// StateController::Delegate implementation that records requested actions.
class TestDelegate : public StateController::Delegate, public ActionRecorder {
 public:
  TestDelegate()
      : record_metrics_actions_(false),
        usb_input_device_connected_(false),
        oobe_completed_(true),
        hdmi_audio_active_(false),
        headphone_jack_plugged_(false),
        lid_state_(LID_OPEN) {
  }
  virtual ~TestDelegate() {}

  void set_record_metrics_actions(bool record) {
    record_metrics_actions_ = record;
  }
  void set_usb_input_device_connected(bool connected) {
    usb_input_device_connected_ = connected;
  }
  void set_oobe_completed(bool completed) {
    oobe_completed_ = completed;
  }
  void set_hdmi_audio_active(bool active) { hdmi_audio_active_ = active; }
  void set_headphone_jack_plugged(bool plugged) {
    headphone_jack_plugged_ = plugged;
  }
  void set_lid_state(LidState state) { lid_state_ = state; }

  // StateController::Delegate overrides:
  bool IsUsbInputDeviceConnected() override {
    return usb_input_device_connected_;
  }
  LidState QueryLidState() override { return lid_state_; }
  bool IsOobeCompleted() override { return oobe_completed_; }
  bool IsHdmiAudioActive() override { return hdmi_audio_active_; }
  bool IsHeadphoneJackPlugged() override {
    return headphone_jack_plugged_;
  }
  void DimScreen() override { AppendAction(kScreenDim); }
  void UndimScreen() override { AppendAction(kScreenUndim); }
  void TurnScreenOff() override { AppendAction(kScreenOff); }
  void TurnScreenOn() override { AppendAction(kScreenOn); }
  void LockScreen() override { AppendAction(kScreenLock); }
  void Suspend() override { AppendAction(kSuspend); }
  void StopSession() override { AppendAction(kStopSession); }
  void ShutDown() override { AppendAction(kShutDown); }
  void UpdatePanelForDockedMode(bool docked) override {
    AppendAction(docked ? kDocked : kUndocked);
  }
  void EmitIdleActionImminent(base::TimeDelta time_until_idle_action) override {
    AppendAction(GetIdleImminentAction(time_until_idle_action));
  }
  void EmitIdleActionDeferred() override {
    AppendAction(kIdleDeferred);
  }
  void ReportUserActivityMetrics() override {
    if (record_metrics_actions_)
      AppendAction(kReportUserActivityMetrics);
  }

 private:
  // Should calls to ReportUserActivityMetrics() be recorded in |actions_|?
  // These are noisy, so by default, they aren't recorded.
  bool record_metrics_actions_;

  // Should IsUsbInputDeviceConnected() return true?
  bool usb_input_device_connected_;

  // Should IsOobeCompleted() return true?
  bool oobe_completed_;

  // Should IsHdmiAudioActive() return true?
  bool hdmi_audio_active_;

  // Should IsHeadphoneJackPlugged() return true?
  bool headphone_jack_plugged_;

  // Lid state to be returned by QueryLidState().
  LidState lid_state_;

  DISALLOW_COPY_AND_ASSIGN(TestDelegate);
};

}  // namespace

class StateControllerTest : public testing::Test {
 public:
  StateControllerTest()
      : test_api_(&controller_),
        now_(base::TimeTicks::FromInternalValue(1000)),
        default_ac_suspend_delay_(base::TimeDelta::FromSeconds(120)),
        default_ac_screen_off_delay_(base::TimeDelta::FromSeconds(100)),
        default_ac_screen_dim_delay_(base::TimeDelta::FromSeconds(90)),
        default_battery_suspend_delay_(base::TimeDelta::FromSeconds(60)),
        default_battery_screen_off_delay_(base::TimeDelta::FromSeconds(40)),
        default_battery_screen_dim_delay_(base::TimeDelta::FromSeconds(30)),
        default_disable_idle_suspend_(0),
        default_require_usb_input_device_to_suspend_(0),
        default_avoid_suspend_when_headphone_jack_plugged_(0),
        default_ignore_external_policy_(0),
        default_allow_docked_mode_(1),
        initial_power_source_(POWER_AC),
        initial_lid_state_(LID_OPEN),
        initial_display_mode_(DISPLAY_NORMAL),
        send_initial_display_mode_(true),
        send_initial_policy_(true) {
  }

 protected:
  void SetMillisecondPref(const std::string& name, base::TimeDelta value) {
    prefs_.SetInt64(name, value.InMilliseconds());
  }

  // Sets values in |prefs_| based on |default_*| members and initializes
  // |controller_|.
  void Init() {
    SetMillisecondPref(kPluggedSuspendMsPref, default_ac_suspend_delay_);
    SetMillisecondPref(kPluggedOffMsPref, default_ac_screen_off_delay_);
    SetMillisecondPref(kPluggedDimMsPref, default_ac_screen_dim_delay_);
    SetMillisecondPref(kUnpluggedSuspendMsPref, default_battery_suspend_delay_);
    SetMillisecondPref(kUnpluggedOffMsPref, default_battery_screen_off_delay_);
    SetMillisecondPref(kUnpluggedDimMsPref, default_battery_screen_dim_delay_);
    prefs_.SetInt64(kDisableIdleSuspendPref, default_disable_idle_suspend_);
    prefs_.SetInt64(kRequireUsbInputDeviceToSuspendPref,
                    default_require_usb_input_device_to_suspend_);
    prefs_.SetInt64(kAvoidSuspendWhenHeadphoneJackPluggedPref,
                    default_avoid_suspend_when_headphone_jack_plugged_);
    prefs_.SetInt64(kIgnoreExternalPolicyPref, default_ignore_external_policy_);
    prefs_.SetInt64(kAllowDockedModePref, default_allow_docked_mode_);

    test_api_.clock()->set_current_time_for_testing(now_);
    controller_.Init(&delegate_, &prefs_, initial_power_source_,
                     initial_lid_state_);

    if (send_initial_display_mode_)
      controller_.HandleDisplayModeChange(initial_display_mode_);
    if (send_initial_policy_)
      controller_.HandlePolicyChange(initial_policy_);
  }

  // Advances |now_| by |interval_|.
  void AdvanceTime(base::TimeDelta interval) {
    now_ += interval;
    test_api_.clock()->set_current_time_for_testing(now_);
  }

  // Checks that |controller_|'s action timeout is scheduled for |now_| and then
  // runs it.  Returns false if the timeout isn't scheduled or is scheduled for
  // a different time.
  bool TriggerTimeout() WARN_UNUSED_RESULT {
    base::TimeTicks timeout_time = test_api_.action_timer_time();
    if (timeout_time == base::TimeTicks()) {
      LOG(ERROR) << "Ignoring request to trigger unscheduled timeout at "
                 << now_.ToInternalValue();
      return false;
    }
    if (timeout_time != now_) {
      LOG(ERROR) << "Ignoring request to trigger timeout scheduled for "
                 << timeout_time.ToInternalValue() << " at "
                 << now_.ToInternalValue();
      return false;
    }
    test_api_.TriggerActionTimeout();
    return true;
  }

  // Advances |now_| by |interval| and calls TriggerTimeout().
  bool AdvanceTimeAndTriggerTimeout(base::TimeDelta interval)
      WARN_UNUSED_RESULT {
    AdvanceTime(interval);
    return TriggerTimeout();
  }

  // Advances |now_| by |next_delay| minus the last delay passed to this
  // method and calls TriggerTimeout().  This is useful when invoking
  // successive delays: for example, given delays at 2, 4, and 5 minutes,
  // instead of calling AdvanceTimeAndTriggerTimeout() with 2, (4 - 2), and
  // then (5 - 4), StepTimeAndTriggerTimeout() can be called with 2, 4, and
  // 5.  Call ResetLastStepDelay() before a new sequence of delays to reset
  // the "last delay".
  bool StepTimeAndTriggerTimeout(base::TimeDelta next_delay)
      WARN_UNUSED_RESULT {
    AdvanceTime(next_delay - last_step_delay_);
    last_step_delay_ = next_delay;
    return TriggerTimeout();
  }

  // Resets the "last delay" used by StepTimeAndTriggerTimeout().
  void ResetLastStepDelay() { last_step_delay_ = base::TimeDelta(); }

  // Steps through time to trigger the default AC screen dim, off, and
  // suspend timeouts.
  bool TriggerDefaultAcTimeouts() WARN_UNUSED_RESULT {
    ResetLastStepDelay();
    return StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_) &&
        StepTimeAndTriggerTimeout(default_ac_screen_off_delay_) &&
        StepTimeAndTriggerTimeout(default_ac_suspend_delay_);
  }

  FakePrefs prefs_;
  TestDelegate delegate_;
  StateController controller_;
  StateController::TestApi test_api_;

  base::TimeTicks now_;

  // Last delay that was passed to StepTimeAndTriggerTimeout().
  base::TimeDelta last_step_delay_;

  // Preference values.  Tests may change these before calling Init().
  base::TimeDelta default_ac_suspend_delay_;
  base::TimeDelta default_ac_screen_off_delay_;
  base::TimeDelta default_ac_screen_dim_delay_;
  base::TimeDelta default_battery_suspend_delay_;
  base::TimeDelta default_battery_screen_off_delay_;
  base::TimeDelta default_battery_screen_dim_delay_;
  int64_t default_disable_idle_suspend_;
  int64_t default_require_usb_input_device_to_suspend_;
  int64_t default_avoid_suspend_when_headphone_jack_plugged_;
  int64_t default_ignore_external_policy_;
  int64_t default_allow_docked_mode_;

  // Values passed by Init() to StateController::Init().
  PowerSource initial_power_source_;
  LidState initial_lid_state_;

  // Initial display mode to send in Init().
  DisplayMode initial_display_mode_;
  bool send_initial_display_mode_;

  // Initial policy to send in Init().
  PowerManagementPolicy initial_policy_;
  bool send_initial_policy_;
};

// Tests the basic operation of the different delays.
TEST_F(StateControllerTest, BasicDelays) {
  Init();

  // The screen should be dimmed after the configured interval and then undimmed
  // in response to user activity.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // The system should eventually suspend if the user is inactive.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // No further timeouts should be scheduled at this point.
  EXPECT_TRUE(test_api_.action_timer_time().is_null());

  // When the system resumes, the screen should be undimmed and turned back
  // on.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // The screen should be dimmed again after the screen-dim delay.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that the screen isn't dimmed while video is detected.
TEST_F(StateControllerTest, VideoDefersDimming) {
  Init();

  // The screen shouldn't be dimmed while a video is playing.
  const base::TimeDelta kHalfDimDelay = default_ac_screen_dim_delay_ / 2;
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  AdvanceTime(kHalfDimDelay);
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the video stops, the dimming delay should happen as expected.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Video activity should be ignored while the screen is dimmed or off.
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the user starts another video, the dimming delay should fire
  // again after the video stops.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that the screen dims, is turned off, and is locked while audio is
// playing.
TEST_F(StateControllerTest, AudioDefersSuspend) {
  Init();

  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(300);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(310);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(320);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(330);

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);

  // Report audio activity and check that the controller goes through the
  // usual dim->off->lock progression.
  controller_.HandleAudioStateChange(true);
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kLockDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());

  // The next timeout will be set based on the last audio activity time, which
  // was "now" at the time of the last call to UpdateState(). When that timeout
  // occurs, it should schedule another timeout after the idle delay without
  // triggering any actions.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After the audio stops, the controller should wait for the full suspend
  // delay before suspending.
  controller_.HandleAudioStateChange(false);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the system is suspended when the lid is closed.
TEST_F(StateControllerTest, LidCloseSuspendsByDefault) {
  Init();
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // After the lid is opened, the next delay should be screen-dimming (i.e.
  // all timers should be reset).
  delegate_.set_lid_state(LID_OPEN);
  controller_.HandleResume();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(LID_OPEN);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that timeouts are reset when the user logs in or out.
TEST_F(StateControllerTest, SessionStateChangeResetsTimeouts) {
  Init();
  controller_.HandleSessionStateChange(SESSION_STARTED);
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // The screen should be undimmed and turned on when a user logs out.
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // The screen should be dimmed again after the usual delay.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that delays are scaled while presenting and that they return to
// their original values when not presenting.
TEST_F(StateControllerTest, ScaleDelaysWhilePresenting) {
  Init();

  const double kScreenDimFactor = 3.0;
  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(300);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(310);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(320);
  const base::TimeDelta kWarnDelay = base::TimeDelta::FromSeconds(330);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(340);

  const base::TimeDelta kScaledDimDelay = kDimDelay * kScreenDimFactor;
  const base::TimeDelta kDelayDiff = kScaledDimDelay - kDimDelay;
  const base::TimeDelta kScaledOffDelay = kOffDelay + kDelayDiff;
  const base::TimeDelta kScaledLockDelay = kLockDelay + kDelayDiff;
  const base::TimeDelta kScaledWarnDelay = kWarnDelay + kDelayDiff;
  const base::TimeDelta kScaledIdleDelay = kIdleDelay + kDelayDiff;

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_warning_ms(kWarnDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  policy.set_presentation_screen_dim_delay_factor(kScreenDimFactor);
  controller_.HandlePolicyChange(policy);

  controller_.HandleDisplayModeChange(DISPLAY_PRESENTATION);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kScaledDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kScaledOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kScaledLockDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kScaledWarnDelay));
  EXPECT_EQ(GetIdleImminentAction(kScaledIdleDelay - kScaledWarnDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kScaledIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  controller_.HandleDisplayModeChange(DISPLAY_NORMAL);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kLockDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kWarnDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kWarnDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());
}

// Tests that the appropriate delays are used when switching between battery
// and AC power.
TEST_F(StateControllerTest, PowerSourceChange) {
  // Start out on battery power.
  initial_power_source_ = POWER_BATTERY;
  default_battery_screen_dim_delay_ = base::TimeDelta::FromSeconds(60);
  default_battery_screen_off_delay_ = base::TimeDelta::FromSeconds(90);
  default_battery_suspend_delay_ = base::TimeDelta::FromSeconds(100);
  default_ac_screen_dim_delay_ = base::TimeDelta::FromSeconds(120);
  default_ac_screen_off_delay_ = base::TimeDelta::FromSeconds(150);
  default_ac_suspend_delay_ = base::TimeDelta::FromSeconds(160);
  Init();

  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Switch to AC power and check that the AC delays are used instead.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandlePowerSourceChange(POWER_AC);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Resume and wait for the screen to be dimmed.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Switch back to battery.  The controller should treat the power source
  // change as a user action and undim the screen (rather than e.g.
  // suspending immediately since |default_battery_suspend_delay_| has been
  // exceeded) and then proceed through the battery delays.
  controller_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_battery_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that externally-supplied policy supercedes powerd's default prefs.
TEST_F(StateControllerTest, PolicySupercedesPrefs) {
  Init();

  // Set an external policy that disables most delays and instructs the
  // power manager to shut the system down after 10 minutes of inactivity
  // if on AC power or stop the session if on battery power.
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(600);
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(0);
  policy.mutable_ac_delays()->set_screen_dim_ms(0);
  policy.mutable_ac_delays()->set_screen_lock_ms(0);
  *policy.mutable_battery_delays() = policy.ac_delays();
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SHUT_DOWN);
  policy.set_battery_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  policy.set_lid_closed_action(PowerManagementPolicy_Action_DO_NOTHING);
  policy.set_use_audio_activity(false);
  policy.set_use_video_activity(false);
  controller_.HandlePolicyChange(policy);

  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kShutDown, delegate_.GetActions());

  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Wait for half of the idle delay and then report user activity, which
  // should reset the logout timeout.  Audio and video activity should not
  // reset the timeout, however.
  AdvanceTime(kIdleDelay / 2);
  controller_.HandleUserActivity();
  AdvanceTime(kIdleDelay / 2);
  controller_.HandleAudioStateChange(true);
  controller_.HandleVideoActivity();
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay / 2));
  EXPECT_EQ(kShutDown, delegate_.GetActions());

  // The policy's request to do nothing when the lid is closed should be
  // honored.
  controller_.HandleDisplayModeChange(DISPLAY_NORMAL);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Wait 120 seconds and then send an updated policy that dims the screen
  // after 60 seconds.  The screen should dim immediately.
  AdvanceTime(base::TimeDelta::FromSeconds(120));
  policy.mutable_ac_delays()->set_screen_dim_ms(60000);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Switch to battery power, which still has an unset screen-dimming
  // delay.  The screen should undim immediately.
  controller_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // Wait for the idle timeout to be reached and check that the battery
  // idle action is performed.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(base::TimeDelta::FromSeconds(600)));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // Update the policy again to shut down if the lid is closed.  Since the
  // lid is already closed, the system should shut down immediately.
  policy.set_lid_closed_action(PowerManagementPolicy_Action_SHUT_DOWN);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kShutDown, delegate_.GetActions());

  // After setting the "ignore external policy" pref, the defaults should
  // be used.
  prefs_.SetInt64(kIgnoreExternalPolicyPref, 1);
  prefs_.NotifyObservers(kIgnoreExternalPolicyPref);
  controller_.HandlePowerSourceChange(POWER_AC);
  controller_.HandleAudioStateChange(false);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
}

// Test that unset fields in a policy are ignored.
TEST_F(StateControllerTest, PartiallyFilledPolicy) {
  Init();

  // Set a policy that has a very short dimming delay but leaves all other
  // fields unset.
  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(1);
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);

  // The policy's dimming delay should be used, but the rest of the delays
  // should come from prefs.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // Setting an empty policy should revert to the values from the prefs.
  policy.Clear();
  controller_.HandlePolicyChange(policy);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());
}

// Tests that policies that enable audio detection while disabling video
// detection result in the screen getting locked at the expected time but
// defer suspend.
TEST_F(StateControllerTest, PolicyDisablingVideo) {
  Init();

  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(300);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(310);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(320);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(330);

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SUSPEND);
  policy.set_use_audio_activity(true);
  policy.set_use_video_activity(false);
  controller_.HandlePolicyChange(policy);

  // Proceed through the screen-dim, screen-off, and screen-lock delays,
  // reporting video and audio activity along the way.  The screen should
  // be locked (since |use_video_activity| is false).
  controller_.HandleVideoActivity();
  controller_.HandleAudioStateChange(true);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  controller_.HandleVideoActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kLockDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());

  // The system shouldn't suspend until a full |kIdleDelay| after the audio
  // activity stops, since |use_audio_activity| is false.
  controller_.HandleVideoActivity();
  controller_.HandleAudioStateChange(false);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the controller does something reasonable if the lid is closed
// just as the idle delay is reached but before the timeout has fired.
TEST_F(StateControllerTest, SimultaneousIdleAndLidActions) {
  Init();

  // Step through the normal delays.  Just when the suspend delay is about
  // to run, close the lid.  We should only make one suspend attempt.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  AdvanceTime(default_ac_suspend_delay_ - default_ac_screen_off_delay_);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the screen stays on while audio is playing if an HDMI output is
// active.
TEST_F(StateControllerTest, KeepScreenOnForHdmiAudio) {
  Init();

  delegate_.set_hdmi_audio_active(true);
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // The screen should be dimmed but stay on while HDMI is active and audio is
  // playing.
  controller_.HandleAudioStateChange(true);
  AdvanceTime(default_ac_screen_off_delay_);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After audio stops, the screen should turn off after the usual delay.
  controller_.HandleAudioStateChange(false);
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());

  // Audio activity should turn the screen back on.
  controller_.HandleAudioStateChange(true);
  EXPECT_EQ(kScreenOn, delegate_.GetActions());
}

// Tests that the |kRequireUsbInputDeviceToSuspendPref| pref is honored.
TEST_F(StateControllerTest, RequireUsbInputDeviceToSuspend) {
  default_require_usb_input_device_to_suspend_ = 1;
  delegate_.set_usb_input_device_connected(false);
  Init();

  // Advance through the usual delays.  The suspend timeout should
  // trigger as before, but no action should be performed.
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // After a USB input device is connected, the system should suspend as
  // before.
  delegate_.set_usb_input_device_connected(true);
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());
}

// Tests that suspend is deferred before OOBE is completed.
TEST_F(StateControllerTest, DontSuspendBeforeOobeCompleted) {
  delegate_.set_oobe_completed(false);
  Init();

  // The screen should dim and turn off as usual, but the system shouldn't
  // be suspended.
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // Report user activity and mark OOBE as done.  The system should suspend
  // this time.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  delegate_.set_oobe_completed(true);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());
}

// Tests that the disable-idle-suspend pref is honored and overrides policies.
TEST_F(StateControllerTest, DisableIdleSuspend) {
  default_disable_idle_suspend_ = 1;
  Init();
  controller_.HandleSessionStateChange(SESSION_STARTED);

  // With the disable-idle-suspend pref set, the system shouldn't suspend
  // when it's idle.
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // Even after explicitly setting a policy to suspend on idle, the system
  // should still stay up.
  PowerManagementPolicy policy;
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SUSPEND);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Stop-session actions should still be honored.
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // Shutdown actions should be ignored, though.
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SHUT_DOWN);
  controller_.HandlePolicyChange(policy);
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // The controller should watch the pref for changes.  After setting it to
  // 0, the system should shut down due to inactivity.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  prefs_.SetInt64(kDisableIdleSuspendPref, 0);
  prefs_.NotifyObservers(kDisableIdleSuspendPref);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kShutDown, NULL),
            delegate_.GetActions());
}

// Tests that the controller does something reasonable when given delays
// that don't make sense.
TEST_F(StateControllerTest, InvalidDelays) {
  // The dim delay should be less than the off delay, which should be less
  // than the idle delay.  All of those constraints are violated here, so
  // all of the other delays should be capped to the idle delay.
  default_ac_screen_dim_delay_ = base::TimeDelta::FromSeconds(120);
  default_ac_screen_off_delay_ = base::TimeDelta::FromSeconds(110);
  default_ac_suspend_delay_ = base::TimeDelta::FromSeconds(100);
  Init();
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_suspend_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());

  // Policy delays should also be cleaned up.
  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(70);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(50);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(80);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(60);

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);

  // The screen-dim delay should be capped to the screen-off delay, while
  // the screen-lock delay should be ignored since it extends beyond the
  // suspend delay.
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the controller cues the delegate to report metrics when user
// activity is observed.
TEST_F(StateControllerTest, ReportMetrics) {
  delegate_.set_record_metrics_actions(true);
  Init();

  // Various events considered to represent user activity (direct activity,
  // power source changes, presentation mode, etc.) should all trigger
  // metrics.
  controller_.HandleUserActivity();
  EXPECT_EQ(kReportUserActivityMetrics, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandlePowerSourceChange(POWER_BATTERY);
  EXPECT_EQ(JoinActions(kReportUserActivityMetrics, kScreenUndim, NULL),
            delegate_.GetActions());
  AdvanceTime(default_ac_screen_dim_delay_ / 2);
  controller_.HandleDisplayModeChange(DISPLAY_PRESENTATION);
  EXPECT_EQ(kReportUserActivityMetrics, delegate_.GetActions());
}

// Tests that we avoid suspending while headphones are connected when so
// requested.
TEST_F(StateControllerTest, AvoidSuspendForHeadphoneJack) {
  default_avoid_suspend_when_headphone_jack_plugged_ = 1;
  Init();

  // With headphones connected, we shouldn't suspend.
  delegate_.set_headphone_jack_plugged(true);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // Without headphones, we should.
  delegate_.set_headphone_jack_plugged(false);
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());

  // Non-suspend actions should still be performed while headphones are
  // connected.
  controller_.HandleResume();
  delegate_.set_headphone_jack_plugged(true);
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  PowerManagementPolicy policy;
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SHUT_DOWN);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kShutDown, NULL),
            delegate_.GetActions());
}

// Tests that the controller handles being woken from idle-suspend by a
// lid-close event (http://crosbug.com/38011).
TEST_F(StateControllerTest, LidCloseAfterIdleSuspend) {
  Init();
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());

  // Close the lid, which may wake the system.  The controller should
  // re-suspend immediately after it receives the lid-closed event, without
  // turning the screen back on.
  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleResume();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that the controller resuspends after a resume from
// suspend-from-lid-closed if the lid is opened and closed so quickly that
// no events are generated (http://crosbug.com/p/17499).
TEST_F(StateControllerTest, ResuspendAfterLidOpenAndClose) {
  Init();
  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // The lid-closed action should be repeated if the lid is still closed
  // when the system resumes.
  controller_.HandleResume();
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that delays function as expected on a system that lacks a lid and
// that resume is treated as user activity.
TEST_F(StateControllerTest, LidNotPresent) {
  initial_lid_state_ = LID_NOT_PRESENT;
  delegate_.set_lid_state(LID_NOT_PRESENT);
  Init();

  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kSuspend, NULL),
            delegate_.GetActions());
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
}

// Tests that the system doesn't suspend while an update is being applied.
TEST_F(StateControllerTest, AvoidSuspendDuringSystemUpdate) {
  Init();

  // Inform the controller that an update is being applied.  The screen
  // should dim and be turned off, but the system should stay on.
  controller_.HandleUpdaterStateChange(UPDATER_UPDATING);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // When the update has been applied, the system should suspend immediately.
  controller_.HandleUpdaterStateChange(UPDATER_UPDATED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Resume the system and announce another update.
  controller_.HandleResume();
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleUpdaterStateChange(UPDATER_UPDATING);

  // Closing the lid should still suspend.
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Step through all of the timeouts again.
  controller_.HandleResume();
  controller_.HandleLidStateChange(LID_OPEN);
  controller_.HandleUserActivity();
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // The system should also suspend immediately after transitioning from
  // the "updating" state back to "idle" (i.e. the update was unsuccessful).
  controller_.HandleUpdaterStateChange(UPDATER_IDLE);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  controller_.HandleResume();
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());

  // If the idle action is changed to log the user out instead of
  // suspending or shutting down, it should still be performed while an
  // update is in-progress.
  PowerManagementPolicy policy;
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(policy);
  controller_.HandleUpdaterStateChange(UPDATER_UPDATING);
  ASSERT_TRUE(TriggerDefaultAcTimeouts());
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, kStopSession, NULL),
            delegate_.GetActions());
}

// Tests that idle warnings are emitted as requested.
TEST_F(StateControllerTest, IdleWarnings) {
  Init();

  const base::TimeDelta kIdleWarningDelay = base::TimeDelta::FromSeconds(50);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(60);
  const base::TimeDelta kHalfInterval = (kIdleDelay - kIdleWarningDelay) / 2;

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(0);
  policy.mutable_ac_delays()->set_screen_off_ms(0);
  policy.mutable_ac_delays()->set_screen_lock_ms(0);
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kIdleWarningDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(policy);

  // The idle-action-imminent notification should be sent at the requested
  // time.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // The idle-action-deferred notification shouldn't be sent when exiting
  // the inactive state after the idle action has been performed.
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // If the controller exits the inactive state before the idle action is
  // performed, an idle-action-deferred notification should be sent.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  AdvanceTime(kHalfInterval);
  controller_.HandleUserActivity();
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // Let the warning fire again.
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());

  // Increase the warning delay and check that the deferred notification is
  // sent.
  policy.mutable_ac_delays()->set_idle_warning_ms(
      (kIdleWarningDelay + kHalfInterval).InMilliseconds());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // The warning should be sent again when its new delay is reached, and
  // the idle action should be performed at the usual time.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kHalfInterval));
  EXPECT_EQ(GetIdleImminentAction(
                kIdleDelay - (kIdleWarningDelay + kHalfInterval)),
            delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kHalfInterval));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // If the warning delay is cleared after the idle-imminent signal has been
  // sent, an idle-deferred signal should be sent.
  ResetLastStepDelay();
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kIdleWarningDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleUserActivity();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  policy.mutable_ac_delays()->set_idle_warning_ms(0);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // The same signals should be sent again if the delay is added and removed
  // without the time advancing.
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kIdleWarningDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  policy.mutable_ac_delays()->set_idle_warning_ms(0);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // Setting the idle action to "do nothing" should send an idle-deferred
  // message if idle-imminent was already sent.
  controller_.HandleUserActivity();
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kIdleWarningDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_DO_NOTHING);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // Setting the idle action back to "stop session" should cause
  // idle-imminent to get sent again.
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_DO_NOTHING);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // The idle-imminent message shouldn't get sent in the first place when
  // the action is unset.
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // If an action is set after the idle delay has been reached,
  // idle-imminent should be sent immediately and the action should
  // be performed.
  controller_.HandleUserActivity();
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(JoinActions(GetIdleImminentAction(base::TimeDelta()).c_str(),
                        kStopSession, NULL),
            delegate_.GetActions());

  // Let idle-imminent get sent and then increase the idle delay. idle-imminent
  // should be sent again immediately with an updated time-until-idle-action.
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  policy.mutable_ac_delays()->set_idle_ms(2 * kIdleDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(GetIdleImminentAction(2 * kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
}

// Tests that the system avoids suspending on lid-closed when an external
// display is connected.
TEST_F(StateControllerTest, DockedMode) {
  Init();

  // Connect an external display and close the lid.  The internal panel
  // should be turned off, but the system shouldn't suspend.
  controller_.HandleDisplayModeChange(DISPLAY_PRESENTATION);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kDocked, delegate_.GetActions());

  // Open the lid and check that the internal panels turns back on.
  controller_.HandleLidStateChange(LID_OPEN);
  EXPECT_EQ(kUndocked, delegate_.GetActions());

  // Close the lid again and check that the system suspends immediately
  // after the external display is unplugged.
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kDocked, delegate_.GetActions());
  controller_.HandleDisplayModeChange(DISPLAY_NORMAL);
  EXPECT_EQ(JoinActions(kUndocked, kSuspend, NULL), delegate_.GetActions());
}

// Tests that the system does not enable docked mode when allow_docked_mode
// is not set.
TEST_F(StateControllerTest, DisallowDockedMode) {
  default_allow_docked_mode_ = 0;
  Init();

  // Connect an external display and close the lid.  The system should suspend.
  controller_.HandleDisplayModeChange(DISPLAY_PRESENTATION);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that PowerManagementPolicy's
// |user_activity_screen_dim_delay_factor| field is honored.
TEST_F(StateControllerTest, IncreaseDelaysAfterUserActivity) {
  Init();
  controller_.HandleSessionStateChange(SESSION_STARTED);

  // Send a policy where delays are doubled if user activity is observed
  // while the screen is dimmed or soon after it's turned off.
  const base::TimeDelta kDimDelay = base::TimeDelta::FromSeconds(120);
  const base::TimeDelta kOffDelay = base::TimeDelta::FromSeconds(200);
  const base::TimeDelta kLockDelay = base::TimeDelta::FromSeconds(300);
  const base::TimeDelta kIdleWarningDelay = base::TimeDelta::FromSeconds(320);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(330);
  const double kDelayFactor = 2.0;
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(kDimDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_off_ms(kOffDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_screen_lock_ms(kLockDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kIdleWarningDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_SUSPEND);
  policy.set_user_activity_screen_dim_delay_factor(kDelayFactor);
  controller_.HandlePolicyChange(policy);

  // Wait for the screen to dim and then immediately report user activity.
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(kScreenUndim, delegate_.GetActions());

  // This should result in the dimming delay being doubled and its distance
  // to all of the other delays being held constant.
  const base::TimeDelta kScaledDimDelay = kDelayFactor * kDimDelay;
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kScaledDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kOffDelay - kDimDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kLockDelay - kOffDelay));
  EXPECT_EQ(kScreenLock, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleWarningDelay - kLockDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kIdleWarningDelay),
            delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay - kIdleWarningDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());

  // Stop the session, which should unscale the delays.  This time, wait
  // for the screen to get turned off and check that the delays are again
  // lengthened after user activity.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kScaledDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Start another session (to again unscale the delays).  Let the screen
  // get dimmed and turned off, but wait longer than the threshold before
  // reporting user activity.  The delays should be unchanged.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, NULL), delegate_.GetActions());
  controller_.HandleSessionStateChange(SESSION_STARTED);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  AdvanceTime(base::TimeDelta::FromMilliseconds(
      StateController::kUserActivityAfterScreenOffIncreaseDelaysMs + 1000));
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());

  // Shorten the screen off delay after the screen is already off such that
  // we're now outside the window in which user activity should scale the
  // delays.  The delays should still be scaled.
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, NULL), delegate_.GetActions());
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kOffDelay));
  EXPECT_EQ(kScreenOff, delegate_.GetActions());
  const base::TimeDelta kShortOffDelay = kOffDelay -
      base::TimeDelta::FromMilliseconds(
          StateController::kUserActivityAfterScreenOffIncreaseDelaysMs + 1000);
  policy.mutable_ac_delays()->set_screen_off_ms(
      kShortOffDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kScaledDimDelay));
  EXPECT_EQ(kScreenDim, delegate_.GetActions());
}

// Tests that the system is suspended as soon as the display mode is received if
// the lid is closed at startup (e.g. due to powerd crashing during a suspend
// attempt and getting restarted or the user closing the lid while the system is
// booting).
TEST_F(StateControllerTest, SuspendIfLidClosedAtStartup) {
  // Nothing should happen yet; we need to wait to see if the system is about to
  // go into docked mode.
  initial_lid_state_ = LID_CLOSED;
  send_initial_display_mode_ = false;
  Init();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  controller_.HandleDisplayModeChange(DISPLAY_NORMAL);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerInitialStateTimeout());
}

// If the lid is already closed at startup but a notification about presentation
// mode is received soon afterwards, the system should go into docked mode
// instead of suspending (http://crbug.com/277091).
TEST_F(StateControllerTest, EnterDockedModeAtStartup) {
  initial_lid_state_ = LID_CLOSED;
  initial_display_mode_ = DISPLAY_PRESENTATION;
  Init();
  EXPECT_EQ(kDocked, delegate_.GetActions());
  EXPECT_FALSE(test_api_.TriggerInitialStateTimeout());
}

// If the lid is already closed at startup but a display-mode notification never
// arrives, StateController should give up eventually and just suspend the
// system.
TEST_F(StateControllerTest, TimeOutIfInitialDisplayModeNotReceived) {
  initial_lid_state_ = LID_CLOSED;
  send_initial_display_mode_ = false;
  Init();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  EXPECT_TRUE(test_api_.TriggerInitialStateTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// The lid-closed action shouldn't be performed until the initial policy is
// received.
TEST_F(StateControllerTest, WaitForPolicyAtStartup) {
  initial_lid_state_ = LID_CLOSED;
  send_initial_policy_ = false;
  Init();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  PowerManagementPolicy policy;
  policy.set_lid_closed_action(PowerManagementPolicy_Action_DO_NOTHING);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  policy.set_lid_closed_action(PowerManagementPolicy_Action_SHUT_DOWN);
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kShutDown, delegate_.GetActions());
}

// If the initial state timeout occurs before the initial policy is received,
// the default lid-closed action should be performed.
TEST_F(StateControllerTest, TimeOutIfInitialPolicyNotReceived) {
  initial_lid_state_ = LID_CLOSED;
  send_initial_policy_ = false;
  Init();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  EXPECT_TRUE(test_api_.TriggerInitialStateTimeout());
  EXPECT_EQ(kSuspend, delegate_.GetActions());
}

// Tests that user activity is ignored while the lid is closed.  Spurious
// events can apparently be reported as a result of the user closing the
// lid (http://crbug.com/221228).
TEST_F(StateControllerTest, IgnoreUserActivityWhileLidClosed) {
  Init();

  // Wait for the screen to be dimmed and turned off.
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // User activity received while the lid is closed should be ignored.
  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // Resume and go through the same sequence as before, but this time while
  // presenting so that docked mode will be used.
  delegate_.set_lid_state(LID_OPEN);
  controller_.HandleResume();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
  controller_.HandleLidStateChange(LID_OPEN);
  controller_.HandleDisplayModeChange(DISPLAY_PRESENTATION);
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_dim_delay_));
  ASSERT_TRUE(StepTimeAndTriggerTimeout(default_ac_screen_off_delay_));
  EXPECT_EQ(JoinActions(kScreenDim, kScreenOff, NULL), delegate_.GetActions());

  // User activity while docked should turn the screen back on and undim it.
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kDocked, delegate_.GetActions());
  controller_.HandleUserActivity();
  EXPECT_EQ(JoinActions(kScreenUndim, kScreenOn, NULL), delegate_.GetActions());
}

// Tests that active audio activity doesn't result in a very-short timeout due
// to the passage of time between successive measurements of "now" in
// StateController (http://crbug.com/308419).
TEST_F(StateControllerTest, AudioDelay) {
  Init();

  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(600);
  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(0);
  policy.mutable_ac_delays()->set_screen_off_ms(0);
  policy.mutable_ac_delays()->set_screen_lock_ms(0);
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  controller_.HandlePolicyChange(policy);

  // Make "now" advance if GetCurrentTime() is called multiple times; then check
  // that the delay that's scheduled after audio starts is somewhere in the
  // ballpark of kIdleDelay.
  const base::TimeTicks start_time = test_api_.clock()->GetCurrentTime();
  test_api_.clock()->set_time_step_for_testing(
      base::TimeDelta::FromMilliseconds(1));
  controller_.HandleAudioStateChange(true);
  const base::TimeDelta timeout = test_api_.action_timer_time() - start_time;
  EXPECT_GT(timeout.InSeconds(), (kIdleDelay / 2).InSeconds());
  EXPECT_LE(timeout.InSeconds(), kIdleDelay.InSeconds());
}

// Tests that when |wait_for_initial_user_activity| policy field is set,
// inactivity-triggered actions are deferred until user activity is reported.
TEST_F(StateControllerTest, WaitForInitialUserActivity) {
  Init();
  controller_.HandleSessionStateChange(SESSION_STARTED);

  const base::TimeDelta kWarningDelay = base::TimeDelta::FromSeconds(585);
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(600);

  PowerManagementPolicy policy;
  policy.mutable_ac_delays()->set_screen_dim_ms(0);
  policy.mutable_ac_delays()->set_screen_off_ms(0);
  policy.mutable_ac_delays()->set_screen_lock_ms(0);
  policy.mutable_ac_delays()->set_idle_warning_ms(
      kWarningDelay.InMilliseconds());
  policy.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  policy.set_ac_idle_action(PowerManagementPolicy_Action_STOP_SESSION);
  policy.set_wait_for_initial_user_activity(true);
  controller_.HandlePolicyChange(policy);

  // Before user activity is seen, the timeout should be scheduled for the
  // soonest-occurring delay (i.e. the idle warning), but when it fires, no
  // actions should be performed and the timeout should just be scheduled again.
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // After user activity is seen, the delays should take effect.
  controller_.HandleUserActivity();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ResetLastStepDelay();
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kWarningDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // Restart the session and check that the actions are avoided again.
  // User activity reported while the session is stopped should be disregarded.
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  controller_.HandleUserActivity();
  controller_.HandleSessionStateChange(SESSION_STARTED);
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());

  // User activity should again result in the delays taking effect.
  controller_.HandleUserActivity();
  ResetLastStepDelay();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kWarningDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // User activity that is seen before the |wait| field is set should still be
  // honored and result in the delays taking effect.
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  controller_.HandlePolicyChange(PowerManagementPolicy());
  controller_.HandleSessionStateChange(SESSION_STARTED);
  controller_.HandleUserActivity();
  controller_.HandlePolicyChange(policy);
  ResetLastStepDelay();
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kWarningDelay),
            delegate_.GetActions());
  ASSERT_TRUE(StepTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kStopSession, delegate_.GetActions());

  // If |wait| is set after the warning has been sent, the idle-deferred signal
  // should be emitted immediately.
  PowerManagementPolicy policy_without_wait = policy;
  policy_without_wait.set_wait_for_initial_user_activity(false);
  controller_.HandlePolicyChange(policy_without_wait);
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  controller_.HandleSessionStateChange(SESSION_STARTED);
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kWarningDelay));
  EXPECT_EQ(GetIdleImminentAction(kIdleDelay - kWarningDelay),
            delegate_.GetActions());
  controller_.HandlePolicyChange(policy);
  EXPECT_EQ(kIdleDeferred, delegate_.GetActions());

  // |wait| should have no effect when no session is ongoing.
  controller_.HandleSessionStateChange(SESSION_STOPPED);
  ASSERT_TRUE(AdvanceTimeAndTriggerTimeout(kWarningDelay));
}

// Tests that idle and lid-closed "shut down" actions are overridden to instead
// suspend when the TPM dictionary-attack count is high.
TEST_F(StateControllerTest, SuspendInsteadOfShuttingDownForTpmCounter) {
  const base::TimeDelta kIdleDelay = base::TimeDelta::FromSeconds(300);
  initial_policy_.mutable_ac_delays()->set_screen_dim_ms(0);
  initial_policy_.mutable_ac_delays()->set_screen_off_ms(0);
  initial_policy_.mutable_ac_delays()->set_screen_lock_ms(0);
  initial_policy_.mutable_ac_delays()->set_idle_ms(kIdleDelay.InMilliseconds());
  initial_policy_.set_ac_idle_action(PowerManagementPolicy_Action_SHUT_DOWN);
  initial_policy_.set_lid_closed_action(PowerManagementPolicy_Action_SHUT_DOWN);

  const int kThreshold = 10;
  prefs_.SetInt64(kTpmCounterSuspendThresholdPref, kThreshold);
  Init();

  // With the count below the threshold, the "shut down" lid-closed action
  // should be honored.
  controller_.HandleTpmStatus(kThreshold - 1);
  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kShutDown, delegate_.GetActions());
  delegate_.set_lid_state(LID_OPEN);
  controller_.HandleLidStateChange(LID_OPEN);

  // Ditto for the idle action.
  EXPECT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kShutDown, delegate_.GetActions());
  controller_.HandleUserActivity();

  // If the count reaches the threshold, the system should suspend instead of
  // shutting down.
  controller_.HandleTpmStatus(kThreshold);
  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  delegate_.set_lid_state(LID_OPEN);
  controller_.HandleLidStateChange(LID_OPEN);

  EXPECT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kSuspend, delegate_.GetActions());
  controller_.HandleUserActivity();

  // If non-"shut down" actions are set, they shouldn't be overridden.
  initial_policy_.set_ac_idle_action(PowerManagementPolicy_Action_DO_NOTHING);
  initial_policy_.set_lid_closed_action(
      PowerManagementPolicy_Action_STOP_SESSION);
  controller_.HandlePolicyChange(initial_policy_);

  delegate_.set_lid_state(LID_CLOSED);
  controller_.HandleLidStateChange(LID_CLOSED);
  EXPECT_EQ(kStopSession, delegate_.GetActions());
  delegate_.set_lid_state(LID_OPEN);
  controller_.HandleLidStateChange(LID_OPEN);

  EXPECT_TRUE(AdvanceTimeAndTriggerTimeout(kIdleDelay));
  EXPECT_EQ(kNoActions, delegate_.GetActions());
  controller_.HandleUserActivity();
}

}  // namespace policy
}  // namespace power_manager
