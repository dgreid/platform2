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

bool KeyboardBacklightController::TestApi::TriggerHoverTimeout() {
  if (!controller_->hover_timer_.IsRunning())
    return false;

  controller_->hover_timer_.Stop();
  controller_->UpdateState();
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
      session_state_(SESSION_STOPPED),
      dimmed_for_inactivity_(false),
      off_for_inactivity_(false),
      shutting_down_(false),
      docked_(false),
      hovering_(false),
      fullscreen_video_playing_(false),
      max_level_(0),
      current_level_(0),
      user_step_index_(-1),
      percent_for_ambient_light_(100.0),
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

  int64_t hover_delay_ms = 0;
  CHECK(prefs->GetInt64(kKeyboardBacklightKeepOnAfterHoverMsPref,
                        &hover_delay_ms));
  keep_on_after_hover_delay_ =
      base::TimeDelta::FromMilliseconds(hover_delay_ms);

  max_level_ = backlight_->GetMaxBrightnessLevel();
  current_level_ = backlight_->GetCurrentBrightnessLevel();

  // Read the user-settable brightness steps (one per line).
  std::string input_str;
  if (!prefs_->GetString(kKeyboardBacklightUserStepsPref, &input_str))
    LOG(FATAL) << "Failed to read pref " << kKeyboardBacklightUserStepsPref;
  std::vector<std::string> lines;
  base::SplitString(input_str, '\n', &lines);
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
  }

  LOG(INFO) << "Backlight has range [0, " << max_level_ << "] with initial "
            << "level " << current_level_;
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
    UpdateState();
  }

  video_timer_.Stop();
  if (is_fullscreen) {
    video_timer_.Start(FROM_HERE,
        base::TimeDelta::FromMilliseconds(kVideoTimeoutIntervalMs),
        this, &KeyboardBacklightController::HandleVideoTimeout);
  }
}

void KeyboardBacklightController::HandleHoverStateChanged(bool hovering) {
  if (hovering == hovering_)
    return;

  hovering_ = hovering;

  hover_timer_.Stop();
  if (!hovering_) {
    // If the user stopped hovering, start a timer to turn the backlight off in
    // a little while. If this is updated to do something else instead of
    // calling UpdateState(), TestApi::TriggerHoverTimeout() must also be
    // updated.
    last_hover_or_user_activity_time_ = clock_->GetCurrentTime();
    hover_timer_.Start(FROM_HERE, keep_on_after_hover_delay_, this,
                       &KeyboardBacklightController::UpdateState);
  } else {
    last_hover_or_user_activity_time_ = base::TimeTicks();
  }

  UpdateState();
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
  if (supports_hover_ && !hovering_) {
    last_hover_or_user_activity_time_ = clock_->GetCurrentTime();
    hover_timer_.Start(FROM_HERE, keep_on_after_hover_delay_, this,
                       &KeyboardBacklightController::UpdateState);
    UpdateState();
  }
}

void KeyboardBacklightController::HandlePolicyChange(
    const PowerManagementPolicy& policy) {}

void KeyboardBacklightController::HandleChromeStart() {}

void KeyboardBacklightController::SetDimmedForInactivity(bool dimmed) {
  if (dimmed == dimmed_for_inactivity_)
    return;
  dimmed_for_inactivity_ = dimmed;
  UpdateState();
}

void KeyboardBacklightController::SetOffForInactivity(bool off) {
  if (off == off_for_inactivity_)
    return;
  off_for_inactivity_ = off;
  UpdateState();
}

void KeyboardBacklightController::SetSuspended(bool suspended) {}

void KeyboardBacklightController::SetShuttingDown(bool shutting_down) {
  if (shutting_down == shutting_down_)
    return;
  shutting_down_ = shutting_down;
  UpdateState();
}

void KeyboardBacklightController::SetDocked(bool docked) {
  if (docked == docked_)
    return;
  docked_ = docked;
  UpdateState();
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

  return UpdateUndimmedBrightness(TRANSITION_FAST,
                                  BRIGHTNESS_CHANGE_USER_INITIATED);
}

bool KeyboardBacklightController::DecreaseUserBrightness(bool allow_off) {
  LOG(INFO) << "Got user-triggered request to decrease brightness";
  if (user_step_index_ == -1)
    InitUserStepIndex();
  if (user_step_index_ > (allow_off ? 0 : 1))
    user_step_index_--;
  num_user_adjustments_++;

  return UpdateUndimmedBrightness(TRANSITION_FAST,
                                  BRIGHTNESS_CHANGE_USER_INITIATED);
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
  percent_for_ambient_light_ = brightness_percent;
  TransitionStyle transition =
      cause == AmbientLightHandler::CAUSED_BY_AMBIENT_LIGHT ?
      TRANSITION_SLOW : TRANSITION_FAST;
  if (UpdateUndimmedBrightness(transition, BRIGHTNESS_CHANGE_AUTOMATED) &&
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
    UpdateState();
  }
}

void KeyboardBacklightController::HandleVideoTimeout() {
  if (fullscreen_video_playing_)
    VLOG(1) << "Fullscreen video stopped";
  fullscreen_video_playing_ = false;
  UpdateState();
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

bool KeyboardBacklightController::RecentlyHovering() const {
  return !last_hover_or_user_activity_time_.is_null() &&
      (clock_->GetCurrentTime() - last_hover_or_user_activity_time_ <
       keep_on_after_hover_delay_);
}

double KeyboardBacklightController::GetUndimmedPercent() const {
  // If the user has explictly set the brightness, use what they requested.
  if (user_step_index_ != -1)
    return user_steps_[user_step_index_];

  // On systems that can detect hovering, keep the backlight off unless the
  // user's hands are or were recently hovering over the touchpad.
  if (supports_hover_ && !hovering_ && !RecentlyHovering())
    return 0.0;

  return percent_for_ambient_light_;
}

bool KeyboardBacklightController::UpdateUndimmedBrightness(
    TransitionStyle transition,
    BrightnessChangeCause cause) {
  if (shutting_down_|| fullscreen_video_playing_ || off_for_inactivity_ ||
      dimmed_for_inactivity_ || docked_)
    return false;

  return ApplyBrightnessPercent(GetUndimmedPercent(), transition, cause);
}

void KeyboardBacklightController::UpdateState() {
  double percent = 0.0;
  TransitionStyle transition = TRANSITION_SLOW;
  const bool use_user = user_step_index_ != -1;

  if (shutting_down_ || docked_) {
    percent = 0.0;
    transition = TRANSITION_INSTANT;
  } else if (hovering_) {
    // Force the backlight on if the user's hands are hovering over the
    // touchpad.
    percent = GetUndimmedPercent();
    transition = TRANSITION_FAST;
  } else if ((!use_user && fullscreen_video_playing_) ||
             (!use_user && display_brightness_is_zero_) ||
             off_for_inactivity_) {
    percent = 0.0;
  } else if (dimmed_for_inactivity_) {
    percent = std::min(kDimPercent, GetUndimmedPercent());
  } else {
    percent = GetUndimmedPercent();
    // Turn the backlight on quickly if we see user activity without getting
    // notified about hovering first.
    if (RecentlyHovering())
      transition = TRANSITION_FAST;
  }

  ApplyBrightnessPercent(percent, transition, BRIGHTNESS_CHANGE_AUTOMATED);
}

bool KeyboardBacklightController::ApplyBrightnessPercent(
    double percent,
    TransitionStyle transition,
    BrightnessChangeCause cause) {
  int64_t level = PercentToLevel(percent);
  if (level == current_level_)
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

}  // namespace policy
}  // namespace power_manager
