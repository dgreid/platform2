// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/state_controller.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>

#include <base/bind.h>
#include <base/callback.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"

namespace power_manager {
namespace policy {

namespace {

// Time in milliseconds to wait for the display mode and policy after Init() is
// called.
const int kInitialStateTimeoutMs = 10000;

// Returns |time_ms|, a time in milliseconds, as a
// util::TimeDeltaToString()-style string.
std::string MsToString(int64_t time_ms) {
  return util::TimeDeltaToString(base::TimeDelta::FromMilliseconds(time_ms));
}

// Returns the minimum positive value after comparing |a| and |b|.  If one
// is zero or negative, the other is returned.  If both are zero or
// negative, an empty base::TimeDelta is returned.
base::TimeDelta GetMinPositiveTimeDelta(base::TimeDelta a, base::TimeDelta b) {
  if (a > base::TimeDelta()) {
    if (b > base::TimeDelta()) {
      return a < b ? a : b;
    } else {
      return a;
    }
  } else {
    return b;
  }
}

// Helper function for ScheduleActionTimeout() to compute how long to sleep
// before calling UpdateState() to perform the next-occurring action. Given
// |now| and an action that should be performed |action_delay| after
// |last_activity_time|, updates |timeout| to be the minimum of its current
// value and the time to wait before performing the action. Does nothing if
// |action_delay| is unset or if the action should've been performed already.
void UpdateActionTimeout(base::TimeTicks now,
                         base::TimeTicks last_activity_time,
                         base::TimeDelta action_delay,
                         base::TimeDelta* timeout) {
  if (action_delay <= base::TimeDelta())
    return;

  const base::TimeTicks action_time = last_activity_time + action_delay;
  if (action_time > now)
    *timeout = GetMinPositiveTimeDelta(*timeout, action_time - now);
}

// Helper function for UpdateState.  The general pattern here is:
// - If |inactivity_duration| has reached |delay| and
//   |action_already_performed| says that the controller hasn't yet
//   performed the corresponding action, then run |callback| and set
//   |action_already_performed| to ensure that the action doesn't get
//   performed again the next time this is called.
// - If |delay| hasn't been reached, then run |undo_callback| if non-null
//   to undo the action if needed and reset |action_already_performed| so
//   that the action can be performed later.
void HandleDelay(base::TimeDelta delay,
                 base::TimeDelta inactivity_duration,
                 base::Closure callback,
                 base::Closure undo_callback,
                 const std::string& description,
                 const std::string& undo_description,
                 bool* action_already_performed) {
  if (delay > base::TimeDelta() && inactivity_duration >= delay) {
    if (!*action_already_performed) {
      LOG(INFO) << description << " after "
                << util::TimeDeltaToString(inactivity_duration);
      callback.Run();
      *action_already_performed = true;
    }
  } else if (*action_already_performed) {
    if (!undo_callback.is_null()) {
      LOG(INFO) << undo_description;
      undo_callback.Run();
    }
    *action_already_performed = false;
  }
}

// Looks up |name|, an int64_t preference representing milliseconds, in
// |prefs|, and returns it as a base::TimeDelta.  Returns true on success.
bool GetMillisecondPref(PrefsInterface* prefs,
                        const std::string& name,
                        base::TimeDelta* out) {
  DCHECK(prefs);
  DCHECK(out);

  int64_t int_value = 0;
  if (!prefs->GetInt64(name, &int_value))
    return false;

  *out = base::TimeDelta::FromMilliseconds(int_value);
  return true;
}

// Returns a string describing |delays| with each field prefixed by
// |prefix|. Helper method for GetPolicyDebugString().
std::string GetPolicyDelaysDebugString(
    const PowerManagementPolicy::Delays& delays,
    const std::string& prefix) {
  std::string str;
  if (delays.has_screen_dim_ms())
    str += prefix + "_dim=" + MsToString(delays.screen_dim_ms()) + " ";
  if (delays.has_screen_off_ms())
    str += prefix + "_screen_off=" + MsToString(delays.screen_off_ms()) + " ";
  if (delays.has_screen_lock_ms())
    str += prefix + "_lock=" + MsToString(delays.screen_lock_ms()) + " ";
  if (delays.has_idle_warning_ms())
    str += prefix + "_idle_warn=" + MsToString(delays.idle_warning_ms()) + " ";
  if (delays.has_idle_ms())
    str += prefix + "_idle=" + MsToString(delays.idle_ms()) + " ";
  return str;
}

}  // namespace

StateController::TestApi::TestApi(StateController* controller)
    : controller_(controller) {
}

StateController::TestApi::~TestApi() {
  controller_ = NULL;
}

void StateController::TestApi::TriggerActionTimeout() {
  CHECK(controller_->action_timer_.IsRunning());
  controller_->action_timer_.Stop();
  controller_->HandleActionTimeout();
}

bool StateController::TestApi::TriggerInitialStateTimeout() {
  if (!controller_->initial_state_timer_.IsRunning())
    return false;

  controller_->initial_state_timer_.Stop();
  controller_->HandleInitialStateTimeout();
  return true;
}

const int StateController::kUserActivityAfterScreenOffIncreaseDelaysMs = 60000;

// static
std::string StateController::GetPolicyDebugString(
    const PowerManagementPolicy& policy) {
  std::string str = GetPolicyDelaysDebugString(policy.ac_delays(), "ac");
  str += GetPolicyDelaysDebugString(policy.battery_delays(), "battery");

  if (policy.has_ac_idle_action()) {
    str += "ac_idle=" +
        ActionToString(ProtoActionToAction(policy.ac_idle_action())) + " ";
  }
  if (policy.has_battery_idle_action()) {
    str += "battery_idle=" +
        ActionToString(ProtoActionToAction(policy.battery_idle_action())) + " ";
  }
  if (policy.has_lid_closed_action()) {
    str += "lid_closed=" +
        ActionToString(ProtoActionToAction(policy.lid_closed_action())) + " ";
  }
  if (policy.has_use_audio_activity())
    str += "use_audio=" + base::IntToString(policy.use_audio_activity()) + " ";
  if (policy.has_use_video_activity())
    str += "use_video=" + base::IntToString(policy.use_video_activity()) + " ";
  if (policy.has_presentation_screen_dim_delay_factor()) {
    str += "presentation_factor=" +
        base::DoubleToString(policy.presentation_screen_dim_delay_factor()) +
        " ";
  }
  if (policy.has_user_activity_screen_dim_delay_factor()) {
    str += "user_activity_factor=" + base::DoubleToString(
        policy.user_activity_screen_dim_delay_factor()) + " ";
  }
  if (policy.has_wait_for_initial_user_activity()) {
    str += "wait_for_initial_user_activity=" +
        base::IntToString(policy.wait_for_initial_user_activity()) + " ";
  }
  if (policy.has_force_nonzero_brightness_for_user_activity()) {
    str += "force_nonzero_brightness_for_user_activity=" +
        base::IntToString(policy.force_nonzero_brightness_for_user_activity()) +
        " ";
  }

  if (policy.has_reason())
    str += "(" + policy.reason() + ")";

  return str.empty() ? "[empty]" : str;
}

StateController::StateController()
    : delegate_(NULL),
      prefs_(NULL),
      clock_(new Clock),
      initialized_(false),
      got_initial_display_mode_(false),
      got_initial_policy_(false),
      power_source_(POWER_AC),
      lid_state_(LID_NOT_PRESENT),
      session_state_(SESSION_STOPPED),
      updater_state_(UPDATER_IDLE),
      display_mode_(DISPLAY_NORMAL),
      screen_dimmed_(false),
      screen_turned_off_(false),
      requested_screen_lock_(false),
      sent_idle_warning_(false),
      idle_action_performed_(false),
      lid_closed_action_performed_(false),
      turned_panel_off_for_docked_mode_(false),
      resend_idle_warning_(false),
      saw_user_activity_soon_after_screen_dim_or_off_(false),
      saw_user_activity_during_current_session_(false),
      require_usb_input_device_to_suspend_(false),
      avoid_suspend_when_headphone_jack_plugged_(false),
      disable_idle_suspend_(false),
      allow_docked_mode_(false),
      ignore_external_policy_(false),
      tpm_dictionary_attack_count_(0),
      tpm_dictionary_attack_suspend_threshold_(0),
      audio_is_active_(false),
      idle_action_(DO_NOTHING),
      lid_closed_action_(DO_NOTHING),
      use_audio_activity_(true),
      use_video_activity_(true),
      wait_for_initial_user_activity_(false) {
}

StateController::~StateController() {
  if (prefs_)
    prefs_->RemoveObserver(this);
}

void StateController::Init(Delegate* delegate,
                           PrefsInterface* prefs,
                           PowerSource power_source,
                           LidState lid_state) {
  delegate_ = delegate;
  prefs_ = prefs;
  prefs_->AddObserver(this);
  LoadPrefs();

  last_user_activity_time_ = clock_->GetCurrentTime();
  power_source_ = power_source;
  lid_state_ = lid_state;

  initial_state_timer_.Start(FROM_HERE,
      base::TimeDelta::FromMilliseconds(kInitialStateTimeoutMs), this,
      &StateController::HandleInitialStateTimeout);

  UpdateSettingsAndState();
  initialized_ = true;
}

void StateController::HandlePowerSourceChange(PowerSource source) {
  CHECK(initialized_);
  if (source == power_source_)
    return;

  power_source_ = source;
  UpdateLastUserActivityTime();
  UpdateSettingsAndState();
}

void StateController::HandleLidStateChange(LidState state) {
  CHECK(initialized_);
  if (state == lid_state_)
    return;

  lid_state_ = state;
  if (state == LID_OPEN)
    UpdateLastUserActivityTime();
  UpdateState();
}

void StateController::HandleSessionStateChange(SessionState state) {
  CHECK(initialized_);
  if (state == session_state_)
    return;

  session_state_ = state;
  saw_user_activity_soon_after_screen_dim_or_off_ = false;
  saw_user_activity_during_current_session_ = false;
  UpdateLastUserActivityTime();
  UpdateSettingsAndState();
}

void StateController::HandleUpdaterStateChange(UpdaterState state) {
  CHECK(initialized_);
  if (state == updater_state_)
    return;

  updater_state_ = state;
  UpdateSettingsAndState();
}

void StateController::HandleDisplayModeChange(DisplayMode mode) {
  CHECK(initialized_);
  if (mode == display_mode_ && got_initial_display_mode_)
    return;

  display_mode_ = mode;

  if (!got_initial_display_mode_) {
    got_initial_display_mode_ = true;
    MaybeStopInitialStateTimer();
  } else {
    UpdateLastUserActivityTime();
  }

  UpdateSettingsAndState();
}

void StateController::HandleResume() {
  CHECK(initialized_);
  switch (delegate_->QueryLidState()) {
    case LID_OPEN:  // fallthrough
    case LID_NOT_PRESENT:
      // Undim the screen and turn it back on immediately after the user
      // opens the lid or wakes the system through some other means.
      UpdateLastUserActivityTime();
      break;
    case LID_CLOSED:
      // If the lid is closed to suspend the machine and then very quickly
      // opened and closed again, the machine may resume without lid-opened
      // and lid-closed events being generated.  Ensure that we're able to
      // resuspend immediately in this case.
      if (lid_state_ == LID_CLOSED &&
          lid_closed_action_ == SUSPEND &&
          lid_closed_action_performed_) {
        LOG(INFO) << "Lid still closed after resuming from lid-close-triggered "
                  << "suspend; repeating lid-closed action";
        lid_closed_action_performed_ = false;
      }
      break;
  }

  UpdateState();
}

void StateController::HandlePolicyChange(const PowerManagementPolicy& policy) {
  CHECK(initialized_);
  policy_ = policy;
  if (!got_initial_policy_) {
    got_initial_policy_ = true;
    MaybeStopInitialStateTimer();
  }
  UpdateSettingsAndState();
}

void StateController::HandleUserActivity() {
  CHECK(initialized_);

  // Ignore user activity reported while the lid is closed unless we're in
  // docked mode.
  if (lid_state_ == LID_CLOSED && !in_docked_mode()) {
    LOG(WARNING) << "Ignoring user activity received while lid is closed";
    return;
  }

  const bool old_saw_user_activity =
      saw_user_activity_soon_after_screen_dim_or_off_;
  const bool screen_turned_off_recently =
      delays_.screen_off > base::TimeDelta() && screen_turned_off_ &&
      (clock_->GetCurrentTime() - screen_turned_off_time_).InMilliseconds() <=
      kUserActivityAfterScreenOffIncreaseDelaysMs;
  if (!saw_user_activity_soon_after_screen_dim_or_off_ &&
      ((screen_dimmed_ && !screen_turned_off_) || screen_turned_off_recently)) {
    LOG(INFO) << "Scaling delays due to user activity while screen was dimmed "
              << "or soon after it was turned off";
    saw_user_activity_soon_after_screen_dim_or_off_ = true;
  }

  if (session_state_ == SESSION_STARTED)
    saw_user_activity_during_current_session_ = true;

  UpdateLastUserActivityTime();
  if (old_saw_user_activity != saw_user_activity_soon_after_screen_dim_or_off_)
    UpdateSettingsAndState();
  else
    UpdateState();
}

void StateController::HandleVideoActivity() {
  CHECK(initialized_);
  if (screen_dimmed_ || screen_turned_off_) {
    LOG(INFO) << "Ignoring video since screen is dimmed or off";
    return;
  }
  last_video_activity_time_ = clock_->GetCurrentTime();
  UpdateState();
}

void StateController::HandleAudioStateChange(bool active) {
  CHECK(initialized_);
  if (active)
    audio_inactive_time_ = base::TimeTicks();
  else if (audio_is_active_)
    audio_inactive_time_ = clock_->GetCurrentTime();
  audio_is_active_ = active;
  UpdateState();
}

void StateController::HandleTpmStatus(int dictionary_attack_count) {
  tpm_dictionary_attack_count_ = dictionary_attack_count;
  UpdateSettingsAndState();
}

void StateController::OnPrefChanged(const std::string& pref_name) {
  CHECK(initialized_);
  if (pref_name == kDisableIdleSuspendPref ||
      pref_name == kIgnoreExternalPolicyPref) {
    LOG(INFO) << "Reloading prefs for " << pref_name << " change";
    LoadPrefs();
    UpdateSettingsAndState();
  }
}

// static
std::string StateController::ActionToString(Action action) {
  switch (action) {
    case SUSPEND:
      return "suspend";
    case STOP_SESSION:
      return "logout";
    case SHUT_DOWN:
      return "shutdown";
    case DO_NOTHING:
      return "no-op";
    default:
      return base::StringPrintf("unknown (%d)", action);
  }
}

// static
StateController::Action StateController::ProtoActionToAction(
    PowerManagementPolicy_Action proto_action) {
  switch (proto_action) {
    case PowerManagementPolicy_Action_SUSPEND:
      return SUSPEND;
    case PowerManagementPolicy_Action_STOP_SESSION:
      return STOP_SESSION;
    case PowerManagementPolicy_Action_SHUT_DOWN:
      return SHUT_DOWN;
    case PowerManagementPolicy_Action_DO_NOTHING:
      return DO_NOTHING;
    default:
      NOTREACHED() << "Unhandled action " << proto_action;
      return DO_NOTHING;
  }
}

// static
void StateController::ScaleDelays(Delays* delays,
                                  double screen_dim_scale_factor) {
  DCHECK(delays);
  if (screen_dim_scale_factor <= 1.0 || delays->screen_dim <= base::TimeDelta())
    return;

  const base::TimeDelta orig_screen_dim = delays->screen_dim;
  delays->screen_dim *= screen_dim_scale_factor;

  const base::TimeDelta diff = delays->screen_dim - orig_screen_dim;
  if (delays->screen_off > base::TimeDelta())
    delays->screen_off += diff;
  if (delays->screen_lock > base::TimeDelta())
    delays->screen_lock += diff;
  if (delays->idle_warning > base::TimeDelta())
    delays->idle_warning += diff;
  if (delays->idle > base::TimeDelta())
    delays->idle += diff;
}

// static
void StateController::SanitizeDelays(Delays* delays) {
  DCHECK(delays);

  // Don't try to turn the screen off after performing the idle action.
  if (delays->screen_off > base::TimeDelta())
    delays->screen_off = std::min(delays->screen_off, delays->idle);
  else
    delays->screen_off = base::TimeDelta();

  // Similarly, don't try to dim the screen after turning it off.
  if (delays->screen_dim > base::TimeDelta()) {
    delays->screen_dim = std::min(
        delays->screen_dim,
        GetMinPositiveTimeDelta(delays->idle, delays->screen_off));
  } else {
    delays->screen_dim = base::TimeDelta();
  }

  // Cap the idle-warning timeout to the idle-action timeout.
  if (delays->idle_warning > base::TimeDelta())
    delays->idle_warning = std::min(delays->idle_warning, delays->idle);
  else
    delays->idle_warning = base::TimeDelta();

  // If the lock delay matches or exceeds the idle delay, unset it --
  // Chrome's lock-before-suspend setting should be enabled instead.
  if (delays->screen_lock >= delays->idle ||
      delays->screen_lock < base::TimeDelta()) {
    delays->screen_lock = base::TimeDelta();
  }
}

// static
void StateController::MergeDelaysFromPolicy(
    const PowerManagementPolicy::Delays& policy_delays,
    Delays* delays_out) {
  DCHECK(delays_out);

  if (policy_delays.has_idle_ms() && policy_delays.idle_ms() >= 0) {
    delays_out->idle =
        base::TimeDelta::FromMilliseconds(policy_delays.idle_ms());
  }
  if (policy_delays.has_idle_warning_ms() &&
      policy_delays.idle_warning_ms() >= 0) {
    delays_out->idle_warning =
        base::TimeDelta::FromMilliseconds(policy_delays.idle_warning_ms());
  }
  if (policy_delays.has_screen_dim_ms() && policy_delays.screen_dim_ms() >= 0) {
    delays_out->screen_dim =
        base::TimeDelta::FromMilliseconds(policy_delays.screen_dim_ms());
  }
  if (policy_delays.has_screen_off_ms() && policy_delays.screen_off_ms() >= 0) {
    delays_out->screen_off =
        base::TimeDelta::FromMilliseconds(policy_delays.screen_off_ms());
  }
  if (policy_delays.has_screen_lock_ms() &&
      policy_delays.screen_lock_ms() >= 0) {
    delays_out->screen_lock =
        base::TimeDelta::FromMilliseconds(policy_delays.screen_lock_ms());
  }
}

void StateController::MaybeStopInitialStateTimer() {
  if (got_initial_display_mode_ && got_initial_policy_)
    initial_state_timer_.Stop();
}

base::TimeTicks StateController::GetLastAudioActivityTime(
    base::TimeTicks now) const {
  // Unlike user and video activity, which are reported as discrete events,
  // audio activity is only reported when it starts or stops. If audio is
  // currently active, report the last-active time as |now|. This means that
  // a timeout will be scheduled unnecessarily, but if audio is still active
  // later, the subsequent call to UpdateState() will again see audio as
  // recently being active and not perform any actions.
  return audio_is_active_ ? now : audio_inactive_time_;
}

base::TimeTicks StateController::GetLastActivityTimeForIdle(
    base::TimeTicks now) const {
  base::TimeTicks last_time =
      waiting_for_initial_user_activity() ? now : last_user_activity_time_;
  if (use_audio_activity_)
    last_time = std::max(last_time, GetLastAudioActivityTime(now));
  if (use_video_activity_)
    last_time = std::max(last_time, last_video_activity_time_);
  return last_time;
}

base::TimeTicks StateController::GetLastActivityTimeForScreenDimOrLock(
    base::TimeTicks now) const {
  base::TimeTicks last_time =
      waiting_for_initial_user_activity() ? now : last_user_activity_time_;
  if (use_video_activity_)
    last_time = std::max(last_time, last_video_activity_time_);
  return last_time;
}

base::TimeTicks StateController::GetLastActivityTimeForScreenOff(
    base::TimeTicks now) const {
  base::TimeTicks last_time =
      waiting_for_initial_user_activity() ? now : last_user_activity_time_;
  if (use_video_activity_)
    last_time = std::max(last_time, last_video_activity_time_);
  if (delegate_->IsHdmiAudioActive())
    last_time = std::max(last_time, GetLastAudioActivityTime(now));
  return last_time;
}

void StateController::UpdateLastUserActivityTime() {
  last_user_activity_time_ = clock_->GetCurrentTime();
  delegate_->ReportUserActivityMetrics();
}

void StateController::LoadPrefs() {
  prefs_->GetBool(kRequireUsbInputDeviceToSuspendPref,
                  &require_usb_input_device_to_suspend_);
  prefs_->GetBool(kAvoidSuspendWhenHeadphoneJackPluggedPref,
                  &avoid_suspend_when_headphone_jack_plugged_);
  prefs_->GetBool(kDisableIdleSuspendPref, &disable_idle_suspend_);
  prefs_->GetBool(kIgnoreExternalPolicyPref, &ignore_external_policy_);
  prefs_->GetBool(kAllowDockedModePref, &allow_docked_mode_);

  int64_t tpm_threshold = 0;
  prefs_->GetInt64(kTpmCounterSuspendThresholdPref, &tpm_threshold);
  tpm_dictionary_attack_suspend_threshold_ = static_cast<int>(tpm_threshold);

  CHECK(GetMillisecondPref(prefs_, kPluggedSuspendMsPref,
                           &pref_ac_delays_.idle));
  CHECK(GetMillisecondPref(prefs_, kPluggedOffMsPref,
                           &pref_ac_delays_.screen_off));
  CHECK(GetMillisecondPref(prefs_, kPluggedDimMsPref,
                           &pref_ac_delays_.screen_dim));

  CHECK(GetMillisecondPref(prefs_, kUnpluggedSuspendMsPref,
                           &pref_battery_delays_.idle));
  CHECK(GetMillisecondPref(prefs_, kUnpluggedOffMsPref,
                           &pref_battery_delays_.screen_off));
  CHECK(GetMillisecondPref(prefs_, kUnpluggedDimMsPref,
                           &pref_battery_delays_.screen_dim));

  SanitizeDelays(&pref_ac_delays_);
  SanitizeDelays(&pref_battery_delays_);

  // Don't wait around for the external policy if the controller has been
  // instructed to ignore it.
  if (ignore_external_policy_) {
    got_initial_policy_ = true;
    MaybeStopInitialStateTimer();
  }
}

void StateController::UpdateSettingsAndState() {
  const Action old_idle_action = idle_action_;
  const Action old_lid_closed_action = lid_closed_action_;
  const base::TimeDelta old_idle_delay = delays_.idle;

  const bool on_ac = power_source_ == POWER_AC;
  const bool presenting = display_mode_ == DISPLAY_PRESENTATION;

  // Start out with the defaults loaded from the power manager's prefs.
  idle_action_ = SUSPEND;
  lid_closed_action_ = SUSPEND;
  delays_ = on_ac ? pref_ac_delays_ : pref_battery_delays_;
  use_audio_activity_ = true;
  use_video_activity_ = true;
  wait_for_initial_user_activity_ = false;
  double presentation_factor = 1.0;
  double user_activity_factor = 1.0;

  // Now update them with values that were set in the policy.
  if (!ignore_external_policy_) {
    if (on_ac && policy_.has_ac_idle_action())
      idle_action_ = ProtoActionToAction(policy_.ac_idle_action());
    else if (!on_ac && policy_.has_battery_idle_action())
      idle_action_ = ProtoActionToAction(policy_.battery_idle_action());
    if (policy_.has_lid_closed_action())
      lid_closed_action_ = ProtoActionToAction(policy_.lid_closed_action());

    if (on_ac && policy_.has_ac_delays())
      MergeDelaysFromPolicy(policy_.ac_delays(), &delays_);
    else if (!on_ac && policy_.has_battery_delays())
      MergeDelaysFromPolicy(policy_.battery_delays(), &delays_);

    if (policy_.has_use_audio_activity())
      use_audio_activity_ = policy_.use_audio_activity();
    if (policy_.has_use_video_activity())
      use_video_activity_ = policy_.use_video_activity();
    if (policy_.has_presentation_screen_dim_delay_factor())
      presentation_factor = policy_.presentation_screen_dim_delay_factor();
    if (policy_.has_user_activity_screen_dim_delay_factor())
      user_activity_factor = policy_.user_activity_screen_dim_delay_factor();
    if (policy_.has_wait_for_initial_user_activity()) {
      wait_for_initial_user_activity_ =
          policy_.wait_for_initial_user_activity();
    }
  }

  if (presenting)
    ScaleDelays(&delays_, presentation_factor);
  else if (saw_user_activity_soon_after_screen_dim_or_off_)
    ScaleDelays(&delays_, user_activity_factor);

  // The disable-idle-suspend pref overrides |policy_|. Note that it also
  // prevents the system from shutting down on idle if no session has been
  // started.
  if (disable_idle_suspend_ &&
      (idle_action_ == SUSPEND || idle_action_ == SHUT_DOWN))
    idle_action_ = DO_NOTHING;

  // Avoid suspending or shutting down due to inactivity while a system
  // update is being applied on AC power so users on slow connections can
  // get updates.  Continue suspending on lid-close so users don't get
  // confused, though.
  if (updater_state_ == UPDATER_UPDATING && on_ac &&
      (idle_action_ == SUSPEND || idle_action_ == SHUT_DOWN))
    idle_action_ = DO_NOTHING;

  // Ignore the lid being closed while presenting to support docked mode.
  if (allow_docked_mode_ && presenting)
    lid_closed_action_ = DO_NOTHING;

  // Override the idle and lid-closed actions to suspend instead of shutting
  // down if the TPM dictionary-attack counter is high.
  if (tpm_dictionary_attack_suspend_threshold_ > 0 &&
      tpm_dictionary_attack_count_ >=
      tpm_dictionary_attack_suspend_threshold_) {
    LOG(WARNING) << "TPM dictionary attack count is "
                 << tpm_dictionary_attack_count_ << " (threshold is "
                 << tpm_dictionary_attack_suspend_threshold_ << "); "
                 << "overriding actions to suspend instead of shutting down";
    if (idle_action_ == SHUT_DOWN)
      idle_action_ = SUSPEND;
    if (lid_closed_action_ == SHUT_DOWN)
      lid_closed_action_ = SUSPEND;
  }

  // If the idle or lid-closed actions changed, make sure that we perform
  // the new actions in the event that the system is already idle or the
  // lid is already closed.
  if (idle_action_ != old_idle_action)
    idle_action_performed_ = false;
  if (lid_closed_action_ != old_lid_closed_action)
    lid_closed_action_performed_ = false;

  SanitizeDelays(&delays_);

  // Let UpdateState() know if it may need to re-send the warning with an
  // updated time-until-idle-action.
  resend_idle_warning_ = sent_idle_warning_ &&
      delays_.idle_warning != base::TimeDelta() &&
      delays_.idle != old_idle_delay;

  LOG(INFO) << "Updated settings:"
            << " dim=" << util::TimeDeltaToString(delays_.screen_dim)
            << " screen_off=" << util::TimeDeltaToString(delays_.screen_off)
            << " lock=" << util::TimeDeltaToString(delays_.screen_lock)
            << " idle_warn=" << util::TimeDeltaToString(delays_.idle_warning)
            << " idle=" << util::TimeDeltaToString(delays_.idle)
            << " (" << ActionToString(idle_action_) << ")"
            << " lid_closed=" << ActionToString(lid_closed_action_)
            << " use_audio=" << use_audio_activity_
            << " use_video=" << use_video_activity_;
  if (wait_for_initial_user_activity_) {
    LOG(INFO) << "Deferring inactivity-triggered actions until user activity "
              << "is observed each time a session starts";
  }

  UpdateState();
}

void StateController::PerformAction(Action action) {
  switch (action) {
    case SUSPEND:
      delegate_->Suspend();
      break;
    case STOP_SESSION:
      delegate_->StopSession();
      break;
    case SHUT_DOWN:
      delegate_->ShutDown();
      break;
    case DO_NOTHING:
      break;
    default:
      NOTREACHED() << "Unhandled action " << action;
  }
}

void StateController::UpdateState() {
  base::TimeTicks now = clock_->GetCurrentTime();
  base::TimeDelta idle_duration = now - GetLastActivityTimeForIdle(now);
  base::TimeDelta screen_dim_or_lock_duration =
      now - GetLastActivityTimeForScreenDimOrLock(now);
  base::TimeDelta screen_off_duration =
      now - GetLastActivityTimeForScreenOff(now);

  const bool screen_was_dimmed = screen_dimmed_;
  HandleDelay(delays_.screen_dim, screen_dim_or_lock_duration,
              base::Bind(&Delegate::DimScreen, base::Unretained(delegate_)),
              base::Bind(&Delegate::UndimScreen, base::Unretained(delegate_)),
              "Dimming screen", "Undimming screen", &screen_dimmed_);
  if (screen_dimmed_ && !screen_was_dimmed &&
      audio_is_active_ && delegate_->IsHdmiAudioActive()) {
    LOG(INFO) << "Audio is currently being sent to display; screen will not be "
              << "turned off for inactivity";
  }

  const bool screen_was_turned_off = screen_turned_off_;
  HandleDelay(delays_.screen_off, screen_off_duration,
              base::Bind(&Delegate::TurnScreenOff, base::Unretained(delegate_)),
              base::Bind(&Delegate::TurnScreenOn, base::Unretained(delegate_)),
              "Turning screen off", "Turning screen on", &screen_turned_off_);
  if (screen_turned_off_ && !screen_was_turned_off)
    screen_turned_off_time_ = now;
  else if (!screen_turned_off_)
    screen_turned_off_time_ = base::TimeTicks();

  HandleDelay(delays_.screen_lock, screen_dim_or_lock_duration,
              base::Bind(&Delegate::LockScreen, base::Unretained(delegate_)),
              base::Closure(), "Locking screen", "", &requested_screen_lock_);

  // The idle-imminent signal is only emitted if an idle action is set.
  if (delays_.idle_warning > base::TimeDelta() &&
      idle_duration >= delays_.idle_warning &&
      idle_action_ != DO_NOTHING) {
    if (!sent_idle_warning_ || resend_idle_warning_) {
      const base::TimeDelta time_until_idle = delays_.idle - idle_duration;
      LOG(INFO) << "Emitting idle-imminent signal with "
                << util::TimeDeltaToString(time_until_idle) << " after "
                << util::TimeDeltaToString(idle_duration);
      delegate_->EmitIdleActionImminent(time_until_idle);
      sent_idle_warning_ = true;
    }
  } else if (sent_idle_warning_) {
    sent_idle_warning_ = false;
    // When resetting the idle-warning trigger, only emit the idle-deferred
    // signal if the idle action hasn't been performed yet or if it was a
    // no-op action.
    if (!idle_action_performed_ || idle_action_ == DO_NOTHING) {
      LOG(INFO) << "Emitting idle-deferred signal";
      delegate_->EmitIdleActionDeferred();
    }
  }
  resend_idle_warning_ = false;

  bool docked = in_docked_mode();
  if (docked != turned_panel_off_for_docked_mode_) {
    LOG(INFO) << "Turning panel " << (docked ? "off" : "on") << " after "
              << (docked ? "entering" : "leaving") << " docked mode";
    delegate_->UpdatePanelForDockedMode(docked);
    turned_panel_off_for_docked_mode_ = docked;
  }

  Action idle_action_to_perform = DO_NOTHING;
  if (idle_duration >= delays_.idle) {
    if (!idle_action_performed_) {
      idle_action_to_perform = idle_action_;
      if (!delegate_->IsOobeCompleted()) {
        LOG(INFO) << "Not performing idle action without OOBE completed";
        idle_action_to_perform = DO_NOTHING;
      }
      if (idle_action_to_perform == SUSPEND &&
          require_usb_input_device_to_suspend_ &&
          !delegate_->IsUsbInputDeviceConnected()) {
        LOG(INFO) << "Not suspending for idle without USB input device";
        idle_action_to_perform = DO_NOTHING;
      }
      if (idle_action_to_perform == SUSPEND &&
          avoid_suspend_when_headphone_jack_plugged_ &&
          delegate_->IsHeadphoneJackPlugged()) {
        LOG(INFO) << "Not suspending for idle due to headphone jack";
        idle_action_to_perform = DO_NOTHING;
      }
      LOG(INFO) << "Ready to perform idle action ("
                << ActionToString(idle_action_to_perform) << ") after "
                << util::TimeDeltaToString(idle_duration);
      idle_action_performed_ = true;
    }
  } else {
    idle_action_performed_ = false;
  }

  Action lid_closed_action_to_perform = DO_NOTHING;
  // Hold off on the lid-closed action if the initial display mode or policy
  // hasn't been received. powerd starts before Chrome's gotten a chance to
  // configure the displays and send the policy, and we don't want to shut down
  // immediately if the user rebooted with the lid closed.
  if (lid_state_ == LID_CLOSED && !waiting_for_initial_state()) {
    if (!lid_closed_action_performed_) {
      lid_closed_action_to_perform = lid_closed_action_;
      LOG(INFO) << "Ready to perform lid-closed action ("
                << ActionToString(lid_closed_action_to_perform) << ")";
      lid_closed_action_performed_ = true;
    }
  } else {
    lid_closed_action_performed_ = false;
  }

  if (idle_action_to_perform == SHUT_DOWN ||
      lid_closed_action_to_perform == SHUT_DOWN) {
    // If either of the actions is shutting down, don't perform the other.
    PerformAction(SHUT_DOWN);
  } else if (idle_action_to_perform == lid_closed_action_to_perform) {
    // If both actions are the same, only perform it once.
    PerformAction(idle_action_to_perform);
  } else {
    // Otherwise, perform both actions.  Note that one or both may be
    // DO_NOTHING.
    PerformAction(idle_action_to_perform);
    PerformAction(lid_closed_action_to_perform);
  }

  ScheduleActionTimeout(now);
}

void StateController::ScheduleActionTimeout(base::TimeTicks now) {
  // Find the minimum of the delays that haven't yet occurred.
  base::TimeDelta timeout_delay;
  UpdateActionTimeout(now, GetLastActivityTimeForScreenDimOrLock(now),
                      delays_.screen_dim, &timeout_delay);
  UpdateActionTimeout(now, GetLastActivityTimeForScreenOff(now),
                      delays_.screen_off, &timeout_delay);
  UpdateActionTimeout(now, GetLastActivityTimeForScreenDimOrLock(now),
                      delays_.screen_lock, &timeout_delay);
  UpdateActionTimeout(now, GetLastActivityTimeForIdle(now),
                      delays_.idle_warning, &timeout_delay);
  UpdateActionTimeout(now, GetLastActivityTimeForIdle(now),
                      delays_.idle, &timeout_delay);

  if (timeout_delay > base::TimeDelta()) {
    action_timer_.Start(FROM_HERE, timeout_delay, this,
        &StateController::HandleActionTimeout);
    action_timer_time_for_testing_ = now + timeout_delay;
  } else {
    action_timer_.Stop();
    action_timer_time_for_testing_ = base::TimeTicks();
  }
}

void StateController::HandleActionTimeout() {
  action_timer_time_for_testing_ = base::TimeTicks();
  UpdateState();
}

void StateController::HandleInitialStateTimeout() {
  LOG(INFO) << "Didn't receive initial notification about display mode or "
            << "policy; using " << DisplayModeToString(display_mode_)
            << " display mode";
  UpdateState();
}

}  // namespace policy
}  // namespace power_manager
