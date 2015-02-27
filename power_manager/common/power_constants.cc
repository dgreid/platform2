// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/common/power_constants.h"

#include <base/logging.h>
#include <base/strings/stringprintf.h>

namespace power_manager {

// Prefs.
const char kLowBatteryShutdownTimePref[] = "low_battery_shutdown_time_s";
const char kLowBatteryShutdownPercentPref[] = "low_battery_shutdown_percent";
const char kPluggedDimMsPref[] = "plugged_dim_ms";
const char kPluggedOffMsPref[] = "plugged_off_ms";
const char kPluggedSuspendMsPref[] = "plugged_suspend_ms";
const char kUnpluggedDimMsPref[] = "unplugged_dim_ms";
const char kUnpluggedOffMsPref[] = "unplugged_off_ms";
const char kUnpluggedSuspendMsPref[] = "unplugged_suspend_ms";
const char kDisableIdleSuspendPref[] = "disable_idle_suspend";
const char kUseLidPref[] = "use_lid";
const char kDetectHoverPref[] = "detect_hover";
const char kRetrySuspendMsPref[] = "retry_suspend_ms";
const char kRetrySuspendAttemptsPref[] = "retry_suspend_attempts";
const char kMinVisibleBacklightLevelPref[] = "min_visible_backlight_level";
const char kInstantTransitionsBelowMinLevelPref[] =
    "instant_transitions_below_min_level";
const char kAvoidSuspendWhenHeadphoneJackPluggedPref[] =
    "avoid_suspend_when_headphone_jack_plugged";
const char kWakeupInputPref[] = "wakeup_input_device_names";
const char kPowerSupplyFullFactorPref[] = "power_supply_full_factor";
const char kInternalBacklightMaxNitsPref[] = "internal_backlight_max_nits";
const char kInternalBacklightAlsStepsPref[] = "internal_backlight_als_steps";
const char kInternalBacklightNoAlsAcBrightnessPref[] =
    "internal_backlight_no_als_ac_brightness";
const char kInternalBacklightNoAlsBatteryBrightnessPref[] =
    "internal_backlight_no_als_battery_brightness";
const char kKeyboardBacklightAlsStepsPref[] = "keyboard_backlight_als_steps";
const char kKeyboardBacklightUserStepsPref[] = "keyboard_backlight_user_steps";
const char kKeyboardBacklightKeepOnAfterHoverMsPref[] =
    "keyboard_backlight_keep_on_after_hover_ms";
const char kRequireUsbInputDeviceToSuspendPref[] =
    "require_usb_input_device_to_suspend";
const char kBatteryPollIntervalPref[] = "battery_poll_interval_ms";
const char kBatteryStabilizedAfterStartupMsPref[] =
    "battery_stabilized_after_startup_ms";
const char kBatteryStabilizedAfterLinePowerConnectedMsPref[] =
    "battery_stabilized_after_line_power_connected_ms";
const char kBatteryStabilizedAfterLinePowerDisconnectedMsPref[] =
    "battery_stabilized_after_line_power_disconnected_ms";
const char kBatteryStabilizedAfterResumeMsPref[] =
    "battery_stabilized_after_resume_ms";
const char kMaxCurrentSamplesPref[] = "max_current_samples";
const char kMaxChargeSamplesPref[] = "max_charge_samples";
const char kTurnOffScreenTimeoutMsPref[] = "turn_off_screen_timeout_ms";
const char kDisableDarkResumePref[] = "disable_dark_resume";
const char kDarkResumeSuspendDurationsPref[] = "dark_resume_suspend_durations";
const char kDarkResumeDevicesPref[] = "dark_resume_devices";
const char kDarkResumeSourcesPref[] = "dark_resume_sources";
const char kIgnoreExternalPolicyPref[] = "ignore_external_policy";
const char kAllowDockedModePref[] = "allow_docked_mode";
const char kNumSessionsOnCurrentChargePref[] = "num_sessions_on_current_charge";
const char kHasAmbientLightSensorPref[] = "has_ambient_light_sensor";
const char kHasKeyboardBacklightPref[] = "has_keyboard_backlight";
const char kExternalDisplayOnlyPref[] = "external_display_only";
const char kLegacyPowerButtonPref[] = "legacy_power_button";
const char kLockVTBeforeSuspendPref[] = "lock_vt_before_suspend";
const char kMosysEventlogPref[] = "mosys_eventlog";
const char kCheckActiveVTPref[] = "check_active_vt";
const char kUseCrasPref[] = "use_cras";
const char kTpmCounterSuspendThresholdPref[] = "tpm_counter_suspend_threshold";

// Miscellaneous constants.
const char kReadWritePrefsDir[] = "/var/lib/power_manager";
const char kReadOnlyPrefsDir[] = "/usr/share/power_manager";
const char kBoardSpecificPrefsSubdir[] = "board_specific";
const char kInternalBacklightPath[] = "/sys/class/backlight";
const char kInternalBacklightPattern[] = "*";
const char kKeyboardBacklightPath[] = "/sys/class/leds";
const char kKeyboardBacklightPattern[] = "*:kbd_backlight";
const char kPowerStatusPath[] = "/sys/class/power_supply";
const double kEpsilon = 0.001;
const int64_t kFastBacklightTransitionMs = 200;
const int64_t kSlowBacklightTransitionMs = 2000;

std::string PowerSourceToString(PowerSource source) {
  switch (source) {
    case POWER_AC:
      return "AC";
    case POWER_BATTERY:
      return "battery";
    default:
      return base::StringPrintf("unknown (%d)", source);
  }
}

std::string LidStateToString(LidState state) {
  switch (state) {
    case LID_OPEN:
      return "open";
    case LID_CLOSED:
      return "closed";
    case LID_NOT_PRESENT:
      return "not present";
    default:
      return base::StringPrintf("unknown (%d)", state);
  }
}

std::string SessionStateToString(SessionState state) {
  switch (state) {
    case SESSION_STOPPED:
      return "stopped";
    case SESSION_STARTED:
      return "started";
    default:
      return base::StringPrintf("unknown (%d)", state);
  }
}

std::string UpdaterStateToString(UpdaterState state) {
  switch (state) {
    case UPDATER_IDLE:
      return "idle";
    case UPDATER_UPDATING:
      return "updating";
    case UPDATER_UPDATED:
      return "updated";
    default:
      return base::StringPrintf("unknown (%d)", state);
  }
}

std::string DisplayModeToString(DisplayMode mode) {
  switch (mode) {
    case DISPLAY_NORMAL:
      return "normal";
    case DISPLAY_PRESENTATION:
      return "presentation";
    default:
      return base::StringPrintf("unknown (%d)", mode);
  }
}

std::string ButtonStateToString(ButtonState state) {
  switch (state) {
    case BUTTON_UP:
      return "up";
    case BUTTON_DOWN:
      return "down";
    case BUTTON_REPEAT:
      return "repeat";
    default:
      return base::StringPrintf("unknown (%d)", state);
  }
}

std::string ShutdownReasonToString(ShutdownReason reason) {
  // These are passed as SHUTDOWN_REASON arguments to an initctl command to
  // switch to runlevel 0 (i.e. don't change these strings without checking that
  // other upstart jobs aren't depending on them).
  switch (reason) {
    case SHUTDOWN_REASON_USER_REQUEST:
      return "user-request";
    case SHUTDOWN_REASON_STATE_TRANSITION:
      return "state-transition";
    case SHUTDOWN_REASON_LOW_BATTERY:
      return "low-battery";
    case SHUTDOWN_REASON_SUSPEND_FAILED:
      return "suspend-failed";
    case SHUTDOWN_REASON_DARK_RESUME:
      return "dark-resume";
    case SHUTDOWN_REASON_SYSTEM_UPDATE:
      return "system-update";
    case SHUTDOWN_REASON_EXIT_DARK_RESUME_FAILED:
      return "exit-dark-resume-failed";
  }
  NOTREACHED() << "Unhandled shutdown reason " << reason;
  return "unknown";
}

}  // namespace power_manager
