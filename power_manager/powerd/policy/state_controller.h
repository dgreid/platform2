// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_STATE_CONTROLLER_H_
#define POWER_MANAGER_POWERD_POLICY_STATE_CONTROLLER_H_

#include <string>

#include <base/compiler_specific.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/time/time.h>
#include <base/timer/timer.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs_observer.h"
#include "power_manager/proto_bindings/policy.pb.h"

namespace power_manager {

class Clock;
class PrefsInterface;

namespace policy {

// StateController is responsible for telling the power manager when to
// perform various actions.
class StateController : public PrefsObserver {
 public:
  // Interface for classes that perform the actions requested by
  // StateController (or otherwise help it interact with the real world).
  class Delegate {
   public:
    virtual ~Delegate() {}

    // Returns true if a USB input devices is connected.
    virtual bool IsUsbInputDeviceConnected() = 0;

    // Returns true if the Chrome OS OOBE (out of box experience) has been
    // completed.
    virtual bool IsOobeCompleted() = 0;

    // Returns true if an HDMI audio output is active. This method does not need
    // to check whether audio is actually currently playing.
    virtual bool IsHdmiAudioActive() = 0;

    // Returns true if a cable is currently plugged in to the headphone jack.
    virtual bool IsHeadphoneJackPlugged() = 0;

    // Returns the current lid state.
    virtual LidState QueryLidState() = 0;

    // Dims the screen in response to the system being idle.
    virtual void DimScreen() = 0;

    // Undoes DimScreen().
    virtual void UndimScreen() = 0;

    // Turns the screen off in response to the system being idle.
    virtual void TurnScreenOff() = 0;

    // Undoes TurnScreenOff().
    virtual void TurnScreenOn() = 0;

    // Requests that the screen be locked.
    virtual void LockScreen() = 0;

    // Suspends the system.
    virtual void Suspend() = 0;

    // Stops the current session, logging the currently-logged-in user out.
    virtual void StopSession() = 0;

    // Shuts the system down.
    virtual void ShutDown() = 0;

    // Turns the internal panel off if the system is currently in "docked
    // mode" and on if the system isn't in docked mode.
    virtual void UpdatePanelForDockedMode(bool docked) = 0;

    // Announces that the idle action will be performed after
    // |time_until_idle_action|.
    virtual void EmitIdleActionImminent(
        base::TimeDelta time_until_idle_action) = 0;

    // Called after EmitIdleActionImminent() if the system left the idle
    // state before the idle action was performed.
    virtual void EmitIdleActionDeferred() = 0;

    // Reports metrics in response to user activity.
    virtual void ReportUserActivityMetrics() = 0;
  };

  class TestApi {
   public:
    explicit TestApi(StateController* controller);
    ~TestApi();

    Clock* clock() { return controller_->clock_.get(); }
    base::TimeTicks action_timer_time() const {
      return controller_->action_timer_time_for_testing_;
    }

    // Runs StateController::HandleActionTimeout(). May only be called if the
    // timer is running.
    void TriggerActionTimeout();

    // Runs StateController::HandleInitialStateTimeout(). Returns false if the
    // timer wasn't running.
    bool TriggerInitialStateTimeout() WARN_UNUSED_RESULT;

   private:
    StateController* controller_;  // weak

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

  // Delays are lengthened if user activity is observed while the screen is
  // dimmed or within this many milliseconds of the screen being turned
  // off.
  static const int kUserActivityAfterScreenOffIncreaseDelaysMs;

  // Returns a string describing |policy|.
  static std::string GetPolicyDebugString(const PowerManagementPolicy& policy);

  StateController();
  virtual ~StateController();

  base::TimeTicks last_user_activity_time() const {
    return last_user_activity_time_;
  }

  // Ownership of |delegate| and |prefs| remains with the caller.
  void Init(Delegate* delegate,
            PrefsInterface* prefs,
            PowerSource power_source,
            LidState lid_state);

  // Handles various changes to external state.
  void HandlePowerSourceChange(PowerSource source);
  void HandleLidStateChange(LidState state);
  void HandleSessionStateChange(SessionState state);
  void HandleUpdaterStateChange(UpdaterState state);
  void HandleDisplayModeChange(DisplayMode mode);
  void HandleResume();
  void HandlePolicyChange(const PowerManagementPolicy& policy);

  // Handles notification of different types of activity.
  void HandleUserActivity();
  void HandleVideoActivity();

  // Handles audio activity starting or stopping.
  void HandleAudioStateChange(bool active);

  // Handles updates to the TPM status.
  void HandleTpmStatus(int dictionary_attack_count);

  // PrefsInterface::Observer implementation:
  void OnPrefChanged(const std::string& pref_name) override;

 private:
  // Holds a collection of delays.
  struct Delays {
    base::TimeDelta idle;
    base::TimeDelta idle_warning;
    base::TimeDelta screen_off;
    base::TimeDelta screen_dim;
    base::TimeDelta screen_lock;
  };

  // These correspond to the identically-named values in the
  // PowerManagementPolicy_Action enum.
  enum Action {
    SUSPEND,
    STOP_SESSION,
    SHUT_DOWN,
    DO_NOTHING,
  };

  static std::string ActionToString(Action action);

  // Converts an Action enum value from a PowerManagementPolicy protocol buffer
  // to the corresponding StateController::Action value.
  static Action ProtoActionToAction(PowerManagementPolicy_Action proto_action);

  // Scales the |screen_dim| delay within |delays| by
  // |screen_dim_scale_factor| and lengthens the other delays to maintain
  // their original distances from the screen-dim delay. Does nothing if
  // |screen_dim_scale_factor| is less than or equal to one or if the
  // dimming delay is unset.
  static void ScaleDelays(Delays* delays, double screen_dim_scale_factor);

  // Adjusts values in |delays| to ensure they make sense.
  static void SanitizeDelays(Delays* delays);

  // Merges set fields from |policy_delays| into |delays_out|, which should
  // already be initialized with default settings.
  static void MergeDelaysFromPolicy(
      const PowerManagementPolicy::Delays& policy_delays,
      Delays* delays_out);

  // Is the system currently in "docked mode", where it remains awake while
  // the lid is closed because an external display is connected?
  bool in_docked_mode() {
    return allow_docked_mode_ && display_mode_ == DISPLAY_PRESENTATION &&
        lid_state_ == LID_CLOSED;
  }

  // Is StateController currently waiting for the display mode and policy to be
  // received for the first time after Init() was called?
  bool waiting_for_initial_state() const {
    return initial_state_timer_.IsRunning();
  }

  // Should inactivity-triggered actions be deferred due to StateController
  // waiting for user activity to be seen during the current session?
  bool waiting_for_initial_user_activity() const {
    return wait_for_initial_user_activity_ &&
        session_state_ == SESSION_STARTED &&
        !saw_user_activity_during_current_session_;
  }

  // Stops |initial_state_timer_| if |got_initial_display_mode_| and
  // |got_initial_policy_| are both true.
  void MaybeStopInitialStateTimer();

  // Returns the last time at which audio was active. If audio is currently
  // active, returns |now|. If audio has never been active, returns a null time.
  base::TimeTicks GetLastAudioActivityTime(base::TimeTicks now) const;

  // Returns the last time at which activity occurred that should defer
  // |idle_action_|, taking |on_ac_|, |use_audio_activity_|, and
  // |use_video_activity_| into account.
  base::TimeTicks GetLastActivityTimeForIdle(base::TimeTicks now) const;

  // Returns the last time at which activity occurred that should defer the
  // screen getting dimmed or locked.
  base::TimeTicks GetLastActivityTimeForScreenDimOrLock(
      base::TimeTicks now) const;

  // Returns the last time at which activity occurred that should defer the
  // screen getting turned off.  This is generally the same as
  // GetLastActivityTimeForScreenDimOrLock() but may differ if the screen needs
  // to be kept on for audio playback.
  base::TimeTicks GetLastActivityTimeForScreenOff(base::TimeTicks now) const;

  // Updates |last_user_activity_time_| to contain the current time and
  // calls |delegate_|'s ReportUserActivityMetrics() method.
  void UpdateLastUserActivityTime();

  // Initializes |require_usb_input_device_to_suspend_| and |pref_*| from
  // |prefs_|.
  void LoadPrefs();

  // Updates in-use settings and calls UpdateState().  Copies values from
  // |pref_*| and then applies externally-provided settings from |policy_|.
  void UpdateSettingsAndState();

  // Instructs |delegate_| to perform |action|.
  void PerformAction(Action action);

  // Ensures that the system is in the correct state, given the times at which
  // activity was last seen, the lid state, the currently-set delays, etc.
  // Invokes ScheduleActionTimeout() when done. If something that affects the
  // current settings has changed, UpdateSettingsAndState() should be called
  // instead.
  void UpdateState();

  // Stops |action_timer_| and resets it to fire when action is next needed,
  // given a current time of |now|.
  void ScheduleActionTimeout(base::TimeTicks now);

  // Invoked by |action_timer_| when it's time to perform an action.
  void HandleActionTimeout();

  // Invoked by |initial_state_timer_| if the current display mode and policy
  // weren't received in a reasonable amount of time after Init() was called.
  void HandleInitialStateTimeout();

  Delegate* delegate_;  // not owned

  PrefsInterface* prefs_;  // not owned

  scoped_ptr<Clock> clock_;

  // Has Init() been called?
  bool initialized_;

  // Have initial values for |display_mode_| and |policy_| been received? The
  // lid-closed action is deferred while waiting for the initial state.
  bool got_initial_display_mode_;
  bool got_initial_policy_;

  // Runs HandleActionTimeout().
  base::OneShotTimer<StateController> action_timer_;

  // Runs HandleInitialStateTimeout().
  base::OneShotTimer<StateController> initial_state_timer_;

  // Time at which |action_timer_| has been scheduled to fire.
  base::TimeTicks action_timer_time_for_testing_;

  // Current power source.
  PowerSource power_source_;

  // Current state of the lid.
  LidState lid_state_;

  // Current user session state.
  SessionState session_state_;

  // Current system update state.
  UpdaterState updater_state_;

  // Whether the system is presenting or not.
  DisplayMode display_mode_;

  // These track whether various actions have already been performed by
  // UpdateState().
  bool screen_dimmed_;
  bool screen_turned_off_;
  bool requested_screen_lock_;
  bool sent_idle_warning_;
  bool idle_action_performed_;
  bool lid_closed_action_performed_;
  bool turned_panel_off_for_docked_mode_;

  // Set to true by UpdateSettingsAndState() if UpdateState() should send
  // another warning if the delay has elapsed, even if |sent_idle_warning_| is
  // true. Warnings contain the remaining time until the idle action will be
  // performed, so they are re-sent when this interval is likely to have
  // changed.
  bool resend_idle_warning_;

  // Time at which the screen was turned off, or null if
  // |screen_turned_off_| is false.  Used for updating
  // |saw_user_activity_soon_after_screen_dim_or_off_|.
  base::TimeTicks screen_turned_off_time_;

  // True if user activity was observed after the screen was dimmed or soon
  // after it was turned off (which can result in delays being lengthened
  // to not annoy the user the next time).  Reset when the session state
  // changes.
  bool saw_user_activity_soon_after_screen_dim_or_off_;

  // True if user activity has been observed during the current session.
  bool saw_user_activity_during_current_session_;

  // Should the system only idle-suspend if a USB input device is
  // connected?  This is controlled by the
  // |kRequireUsbInputDeviceToSuspendPref| pref and set on hardware that
  // doesn't wake in response to Bluetooth input devices.
  bool require_usb_input_device_to_suspend_;

  // Should the system avoid suspending when something is plugged in to the
  // headphone jack? This is controlled by the
  // |kAvoidSuspendWhenHeadphoneJackPluggedPref| pref and set for hardware that
  // generates noise on the headphone jack when suspended.
  bool avoid_suspend_when_headphone_jack_plugged_;

  // Should the system be prevented from suspending in response to
  // inactivity?  This is controlled by the |kDisableIdleSuspendPref| pref
  // and overrides |policy_|.
  bool disable_idle_suspend_;

  // True if docked mode is allowed.
  bool allow_docked_mode_;

  // Should |policy_| be ignored?  Used by tests and developers.
  bool ignore_external_policy_;

  // TPM dictionary-attack counter value.
  int tpm_dictionary_attack_count_;

  // |tpm_dictionary_attack_count_| value at or above which the system will
  // suspend instead of shutting down in some cases (see
  // http://crbug.com/462428), or 0 if disabled.
  int tpm_dictionary_attack_suspend_threshold_;

  base::TimeTicks last_user_activity_time_;
  base::TimeTicks last_video_activity_time_;

  // Is audio currently active?
  bool audio_is_active_;

  // If audio is inactive, the time at which it transitioned from the active
  // state to the inactive state. Unset if audio is active.
  base::TimeTicks audio_inactive_time_;

  // Most recent externally-supplied policy.
  PowerManagementPolicy policy_;

  // Current settings (|pref_*| with |policy_| layered on top).
  Action idle_action_;
  Action lid_closed_action_;
  Delays delays_;
  bool use_audio_activity_;
  bool use_video_activity_;
  bool wait_for_initial_user_activity_;

  // Default settings loaded from prefs.
  Delays pref_ac_delays_;
  Delays pref_battery_delays_;

  DISALLOW_COPY_AND_ASSIGN(StateController);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_STATE_CONTROLLER_H_
