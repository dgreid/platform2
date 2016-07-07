// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/keyboard_backlight_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/backlight_interface.h"

namespace power_manager {
namespace policy {

namespace {

// This is how long after a video playing message is received we should wait
// until reverting to the not playing state. If another message is received in
// this interval the timeout is reset. The browser should be sending these
// messages ~5 seconds when video is playing.
const int64_t kVideoTimeoutIntervalMs = 7000;

// Delay to wait before logging that hovering has stopped. This is ideally
// smaller than kKeyboardBacklightKeepOnMsPref; otherwise the backlight can be
// turned off before the hover-off event that triggered it is logged.
const int64_t kLogHoverOffDelayMs = 20000;

// Returns the total duration for |style|.
base::TimeDelta GetTransitionDuration(
    BacklightController::TransitionStyle style) {
  switch (style) {
    case BacklightController::TRANSITION_INSTANT:
      return base::TimeDelta();
    case BacklightController::TRANSITION_FAST:
      return base::TimeDelta::FromMilliseconds(kFastBacklightTransitionMs);
    case BacklightController::TRANSITION_SLOW:
      return base::TimeDelta::FromMilliseconds(kSlowBacklightTransitionMs);
    default:
      NOTREACHED() << "Unhandled transition style " << style;
      return base::TimeDelta();
  }
}

}  // namespace

KeyboardBacklightController::TestApi::TestApi(
    KeyboardBacklightController* controller)
  : controller_(controller) {
}

KeyboardBacklightController::TestApi::~TestApi() {}

bool KeyboardBacklightController::TestApi::TriggerTurnOffTimeout() {
  if (!controller_->turn_off_timer_.IsRunning())
    return false;

  controller_->turn_off_timer_.Stop();
  controller_->UpdateState(TRANSITION_SLOW, BRIGHTNESS_CHANGE_AUTOMATED);
  return true;
}

bool KeyboardBacklightController::TestApi::TriggerVideoTimeout() {
  if (!controller_->video_timer_.IsRunning())
    return false;

  controller_->video_timer_.Stop();
  controller_->HandleVideoTimeout();
  return true;
}

const double KeyboardBacklightController::kDimPercent = 10.0;

KeyboardBacklightController::KeyboardBacklightController()
    : clock_(new Clock),
      backlight_(NULL),
      prefs_(NULL),
      display_backlight_controller_(NULL),
      supports_hover_(false),
      turn_on_for_user_activity_(false),
      session_state_(SESSION_STOPPED),
      dimmed_for_inactivity_(false),
      off_for_inactivity_(false),
      suspended_(false),
      shutting_down_(false),
      docked_(false),
      hovering_(false),
      fullscreen_video_playing_(false),
      max_level_(0),
      current_level_(0),
      user_step_index_(-1),
      automated_percent_(100.0),
      num_als_adjustments_(0),
      num_user_adjustments_(0),
      display_brightness_is_zero_(false) {
}

KeyboardBacklightController::~KeyboardBacklightController() {
  if (display_backlight_controller_)
    display_backlight_controller_->RemoveObserver(this);
}

void KeyboardBacklightController::Init(
    system::BacklightInterface* backlight,
    PrefsInterface* prefs,
    system::AmbientLightSensorInterface* sensor,
    BacklightController* display_backlight_controller) {
  backlight_ = backlight;
  prefs_ = prefs;

  display_backlight_controller_ = display_backlight_controller;
  if (display_backlight_controller_)
    display_backlight_controller_->AddObserver(this);

  if (sensor) {
    ambient_light_handler_.reset(new AmbientLightHandler(sensor, this));
    ambient_light_handler_->set_name("keyboard");
  }

  prefs_->GetBool(kDetectHoverPref, &supports_hover_);
  prefs_->GetBool(kKeyboardBacklightTurnOnForUserActivityPref,
                  &turn_on_for_user_activity_);

  int64_t delay_ms = 0;
  CHECK(prefs->GetInt64(kKeyboardBacklightKeepOnMsPref, &delay_ms));
  keep_on_delay_ = base::TimeDelta::FromMilliseconds(delay_ms);
  CHECK(prefs->GetInt64(kKeyboardBacklightKeepOnDuringVideoMsPref, &delay_ms));
  keep_on_during_video_delay_ = base::TimeDelta::FromMilliseconds(delay_ms);

  max_level_ = backlight_->GetMaxBrightnessLevel();
  current_level_ = backlight_->GetCurrentBrightnessLevel();
  LOG(INFO) << "Backlight has range [0, " << max_level_ << "] with initial "
            << "level " << current_level_;

  // Read the user-settable brightness steps (one per line).
  std::string input_str;
  if (!prefs_->GetString(kKeyboardBacklightUserStepsPref, &input_str))
    LOG(FATAL) << "Failed to read pref " << kKeyboardBacklightUserStepsPref;
  std::vector<std::string> lines =
      base::SplitString(input_str, "\n", base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_ALL);
  for (std::vector<std::string>::iterator iter = lines.begin();
       iter != lines.end(); ++iter) {
    double new_step = 0.0;
    if (!base::StringToDouble(*iter, &new_step))
      LOG(FATAL) << "Invalid line in pref " << kKeyboardBacklightUserStepsPref
                 << ": \"" << *iter << "\"";
    user_steps_.push_back(util::ClampPercent(new_step));
  }
  CHECK(!user_steps_.empty())
      << "No user brightness steps defined in "
      << kKeyboardBacklightUserStepsPref;

  if (ambient_light_handler_.get()) {
    std::string pref_value;
    CHECK(prefs_->GetString(kKeyboardBacklightAlsStepsPref, &pref_value))
        << "Unable to read pref " << kKeyboardBacklightAlsStepsPref;
    ambient_light_handler_->Init(pref_value, LevelToPercent(current_level_));
  } else {
    automated_percent_ = user_steps_.back();
    prefs_->GetDouble(kKeyboardBacklightNoAlsBrightnessPref,
                      &automated_percent_);
    UpdateState(TRANSITION_SLOW, BRIGHTNESS_CHANGE_AUTOMATED);
  }
}

void KeyboardBacklightController::AddObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void KeyboardBacklightController::RemoveObserver(
    BacklightControllerObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

void KeyboardBacklightController::HandleVideoActivity(bool is_fullscreen) {
  // Ignore fullscreen video that's reported when the user isn't logged in;
  // it may be triggered by animations on the login screen.
  if (is_fullscreen && session_state_ == SESSION_STOPPED)
    is_fullscreen = false;

  if (is_fullscreen != fullscreen_video_playing_) {
    VLOG(1) << "Fullscreen video "
            << (is_fullscreen ? "started" : "went non-fullscreen");
    fullscreen_video_playing_ = is_fullscreen;
    UpdateState(TRANSITION_SLOW, BRIGHTNESS_CHANGE_AUTOMATED);
  }

  video_timer_.Stop();
  if (is_fullscreen) {
    video_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kVideoTimeoutIntervalMs),
        this, &KeyboardBacklightController::HandleVideoTimeout);
  }
}

void KeyboardBacklightController::HandleHoverStateChanged(bool hovering) {
  if (!supports_hover_ || hovering == hovering_)
    return;

  hovering_ = hovering;

  turn_off_timer_.Stop();
  if (!hovering_) {
    // If the user stopped hovering, start a timer to turn the backlight off in
    // a little while. If this is updated to do something else instead of
    // calling UpdateState(), TestApi::TriggerTurnOffTimeout() must also be
    // updated.
    last_hover_time_ = clock_->GetCurrentTime();
    UpdateTurnOffTimer();

    // Hovering can start and stop frequently. To avoid spamming the logs, wait
    // a while before logging that it's stopped.
    log_hover_off_timer_.Start(
        FROM_HERE, base::TimeDelta::FromMilliseconds(kLogHoverOffDelayMs), this,
        &KeyboardBacklightController::LogHoverOff);
  } else {
    last_hover_time_ = base::TimeTicks();

    // Only log that hovering has started if we weren't waiting to log that it'd
    // stopped.
    if (!log_hover_off_timer_.IsRunning())
      LOG(INFO) << "Hovering on";
    else
      log_hover_off_timer_.Stop();
  }

  UpdateState(hovering_ ? TRANSITION_FAST : TRANSITION_SLOW,
              BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::HandlePowerSourceChange(PowerSource source) {}

void KeyboardBacklightController::HandleDisplayModeChange(DisplayMode mode) {}

void KeyboardBacklightController::HandleSessionStateChange(SessionState state) {
  session_state_ = state;
  if (state == SESSION_STARTED) {
    num_als_adjustments_ = 0;
    num_user_adjustments_ = 0;
  }
}

void KeyboardBacklightController::HandlePowerButtonPress() {}

void KeyboardBacklightController::HandleUserActivity(UserActivityType type) {
  last_user_activity_time_ = clock_->GetCurrentTime();
  UpdateTurnOffTimer();
  UpdateState(TRANSITION_FAST, BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {}

void KeyboardBacklightController::HandleChromeStart() {}

void KeyboardBacklightController::SetDimmedForInactivity(bool dimmed) {
  if (dimmed == dimmed_for_inactivity_)
    return;
  dimmed_for_inactivity_ = dimmed;
  UpdateState(TRANSITION_SLOW, BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::SetOffForInactivity(bool off) {
  if (off == off_for_inactivity_)
    return;
  off_for_inactivity_ = off;
  UpdateState(TRANSITION_SLOW, BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::SetSuspended(bool suspended) {
  if (suspended == suspended_)
    return;
  suspended_ = suspended;
  UpdateState(suspended ? TRANSITION_INSTANT : TRANSITION_FAST,
              BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::SetShuttingDown(bool shutting_down) {
  if (shutting_down == shutting_down_)
    return;
  shutting_down_ = shutting_down;
  UpdateState(TRANSITION_INSTANT, BRIGHTNESS_CHANGE_AUTOMATED);
}

void KeyboardBacklightController::SetDocked(bool docked) {
  if (docked == docked_)
    return;
  docked_ = docked;
  UpdateState(TRANSITION_INSTANT, BRIGHTNESS_CHANGE_AUTOMATED);
}

bool KeyboardBacklightController::GetBrightnessPercent(double* percent) {
  DCHECK(percent);
  *percent = LevelToPercent(current_level_);
  return *percent >= 0.0;
}

bool KeyboardBacklightController::SetUserBrightnessPercent(
    double percent,
    TransitionStyle style) {
  // There's currently no UI for setting the keyboard backlight brightness
  // to arbitrary levels; the user is instead just given the option of
  // increasing or decreasing the brightness between pre-defined levels.
  return false;
}

bool KeyboardBacklightController::IncreaseUserBrightness() {
  LOG(INFO) << "Got user-triggered request to increase brightness";
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ < static_cast<int>(user_steps_.size()) - 1)
    user_step_index_++;
  num_user_adjustments_++;

  return UpdateState(TRANSITION_FAST, BRIGHTNESS_CHANGE_USER_INITIATED);
}

bool KeyboardBacklightController::DecreaseUserBrightness(bool allow_off) {
  LOG(INFO) << "Got user-triggered request to decrease brightness";
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ > (allow_off ? 0 : 1))
    user_step_index_--;
  num_user_adjustments_++;

  return UpdateState(TRANSITION_FAST, BRIGHTNESS_CHANGE_USER_INITIATED);
}

int KeyboardBacklightController::GetNumAmbientLightSensorAdjustments() const {
  return num_als_adjustments_;
}

int KeyboardBacklightController::GetNumUserAdjustments() const {
  return num_user_adjustments_;
}

void KeyboardBacklightController::SetBrightnessPercentForAmbientLight(
    double brightness_percent,
    AmbientLightHandler::BrightnessChangeCause cause) {
  automated_percent_ = brightness_percent;
  TransitionStyle transition =
      cause == AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT ?
      TRANSITION_SLOW : TRANSITION_FAST;
  if (UpdateState(transition, BRIGHTNESS_CHANGE_AUTOMATED) &&
      cause == AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT)
    num_als_adjustments_++;
}

void KeyboardBacklightController::OnBrightnessChanged(
    double brightness_percent,
    BacklightController::BrightnessChangeCause cause,
    BacklightController* source) {
  DCHECK_EQ(source, display_backlight_controller_);

  bool zero = brightness_percent <= kEpsilon;
  if (zero != display_brightness_is_zero_) {
    display_brightness_is_zero_ = zero;
    UpdateState(TRANSITION_SLOW, cause);
  }
}

void KeyboardBacklightController::HandleVideoTimeout() {
  if (fullscreen_video_playing_)
    VLOG(1) << "Fullscreen video stopped";
  fullscreen_video_playing_ = false;
  UpdateState(TRANSITION_FAST, BRIGHTNESS_CHANGE_AUTOMATED);
  UpdateTurnOffTimer();
}

int64_t KeyboardBacklightController::PercentToLevel(double percent) const {
  if (max_level_ == 0)
    return -1;
  percent = std::max(std::min(percent, 100.0), 0.0);
  return lround(static_cast<double>(max_level_) * percent / 100.0);
}

double KeyboardBacklightController::LevelToPercent(int64_t level) const {
  if (max_level_ == 0)
    return -1.0;
  level = std::max(std::min(level, max_level_), static_cast<int64_t>(0));
  return static_cast<double>(level) * 100.0 / max_level_;
}

bool KeyboardBacklightController::RecentlyHoveringOrUserActive() const {
  if (hovering_)
    return true;

  const base::TimeTicks now = clock_->GetCurrentTime();
  const base::TimeDelta delay =
      fullscreen_video_playing_ ? keep_on_during_video_delay_ : keep_on_delay_;
  return (!last_hover_time_.is_null() && (now - last_hover_time_ < delay)) ||
         (!last_user_activity_time_.is_null() &&
          (now - last_user_activity_time_ < delay));
}

void KeyboardBacklightController::InitUserStepIndex() {
  if (user_step_index_ != -1)
    return;

  // Find the step nearest to the current backlight level.
  double percent = LevelToPercent(current_level_);
  double percent_delta = std::numeric_limits<double>::max();
  for (size_t i = 0; i < user_steps_.size(); i++) {
    double temp_delta = fabs(percent - user_steps_[i]);
    if (temp_delta < percent_delta) {
      percent_delta = temp_delta;
      user_step_index_ = i;
    }
  }
  CHECK_NE(user_step_index_, -1)
      << "Failed to find brightness step for level " << current_level_;
}

void KeyboardBacklightController::UpdateTurnOffTimer() {
  if (!supports_hover_ && !turn_on_for_user_activity_)
    return;

  turn_off_timer_.Stop();

  // The timer shouldn't start until hovering stops.
  if (hovering_)
    return;

  // Determine how much time is left.
  const base::TimeTicks timeout_start =
      std::max(last_hover_time_, last_user_activity_time_);
  if (timeout_start.is_null())
    return;

  const base::TimeDelta full_delay =
      fullscreen_video_playing_ ? keep_on_during_video_delay_ : keep_on_delay_;
  const base::TimeDelta remaining_delay =
      full_delay - (clock_->GetCurrentTime() - timeout_start);
  if (remaining_delay <= base::TimeDelta::FromMilliseconds(0))
    return;

  turn_off_timer_.Start(
      FROM_HERE, remaining_delay,
      base::Bind(
          base::IgnoreResult(&KeyboardBacklightController::UpdateState),
          base::Unretained(this), TRANSITION_SLOW,
          BRIGHTNESS_CHANGE_AUTOMATED));
}

bool KeyboardBacklightController::UpdateState(TransitionStyle transition,
                                              BrightnessChangeCause cause) {
  // Force the backlight off immediately in several special cases.
  if (shutting_down_ || docked_ || suspended_)
    return ApplyBrightnessPercent(0.0, transition, cause);

  // If the user has asked for a specific brightness level, use it unless the
  // user is inactive.
  if (user_step_index_ != -1) {
    double percent = user_steps_[user_step_index_];
    if ((off_for_inactivity_|| dimmed_for_inactivity_) && !hovering_)
      percent = off_for_inactivity_ ? 0.0 : std::min(kDimPercent, percent);
    return ApplyBrightnessPercent(percent, transition, cause);
  }

  // If requested, force the backlight on if the user is currently or was
  // recently active and off otherwise.
  if (supports_hover_ || turn_on_for_user_activity_) {
    if (RecentlyHoveringOrUserActive())
      return ApplyBrightnessPercent(automated_percent_, transition, cause);
    else
      return ApplyBrightnessPercent(0.0, transition, cause);
  }

  // Force the backlight off for several more lower-priority conditions.
  // TODO(derat): Restructure this so the backlight is kept on for at least a
  // short period after hovering stops while fullscreen video is playing:
  // http://crbug.com/623404.
  if (fullscreen_video_playing_ || display_brightness_is_zero_ ||
      off_for_inactivity_) {
    return ApplyBrightnessPercent(0.0, transition, cause);
  }

  if (dimmed_for_inactivity_) {
    return ApplyBrightnessPercent(std::min(kDimPercent, automated_percent_),
                                  transition, cause);
  }

  return ApplyBrightnessPercent(automated_percent_, transition, cause);
}

bool KeyboardBacklightController::ApplyBrightnessPercent(
    double percent,
    TransitionStyle transition,
    BrightnessChangeCause cause) {
  int64_t level = PercentToLevel(percent);
  if (level == current_level_ && !backlight_->TransitionInProgress())
    return false;

  base::TimeDelta interval = GetTransitionDuration(transition);
  LOG(INFO) << "Setting brightness to " << level << " (" << percent
            << "%) over " << interval.InMilliseconds() << " ms";
  if (!backlight_->SetBrightnessLevel(level, interval)) {
    LOG(ERROR) << "Failed to set brightness";
    return false;
  }

  current_level_ = level;
  FOR_EACH_OBSERVER(BacklightControllerObserver, observers_,
                    OnBrightnessChanged(percent, cause, this));
  return true;
}

void KeyboardBacklightController::LogHoverOff() {
  const base::TimeDelta delay = clock_->GetCurrentTime() - last_hover_time_;
  LOG(INFO) << "Hovering off " << delay.InMilliseconds() << " ms ago";
}

}  // namespace policy
}  // namespace power_manager
