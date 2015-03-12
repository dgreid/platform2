// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
#define POWER_MANAGER_COMMON_POWER_CONSTANTS_H_

#include <stdint.h>

#include <string>

namespace power_manager {

// Preference names.

// Battery seconds-to-empty and percentage (in the range [0.0, 100.0])
// thresholds at which the system should shut down automatically. If both prefs
// are set, only the percent-based pref will take effect.
extern const char kLowBatteryShutdownTimePref[];
extern const char kLowBatteryShutdownPercentPref[];

// Integer default delays for dimming the screen, turning it off, and suspending
// the system while on AC and battery power. Note that these values are
// overriden by policy messages sent from Chrome.
extern const char kPluggedDimMsPref[];
extern const char kPluggedOffMsPref[];
extern const char kPluggedSuspendMsPref[];
extern const char kUnpluggedDimMsPref[];
extern const char kUnpluggedOffMsPref[];
extern const char kUnpluggedSuspendMsPref[];

// If true, the system will not suspend due to user inactivity.
extern const char kDisableIdleSuspendPref[];

// If true, powerd will monitor the lid switch.
extern const char kUseLidPref[];

// If true, powerd will detect hovering if a capable device is present.
extern const char kDetectHoverPref[];

// Integer amount of time that powerd should wait before retrying after a failed
// suspend attempt.
extern const char kRetrySuspendMsPref[];

// Integer number of failed suspend attempts before shutting the system down.
extern const char kRetrySuspendAttemptsPref[];

// Minimum brightness level (in hardware-specific units) that the backlight
// should be remain at before it's turned off entirely. If unset, a default
// based on the maximum brightness level is used.
extern const char kMinVisibleBacklightLevelPref[];

// If true, powerd will jump directly from the min-visible-level to 0 rather
// than animating smoothly.
extern const char kInstantTransitionsBelowMinLevelPref[];

// If true, the system will not be suspended due to user inactivity while
// something is connected to the headphone jack.
extern const char kAvoidSuspendWhenHeadphoneJackPluggedPref[];

// Newline-separated list of names of inputs that wake the system from suspend.
extern const char kWakeupInputPref[];

// Fraction of the battery's total charge, in the range (0.0, 1.0], at which it
// should be reported as full in the UI.
extern const char kPowerSupplyFullFactorPref[];

// Maximum luminance of the internal panel. Optionally set through VPD in the
// factory to allow specifying different default brightness percentages for
// different displays in kInternalBacklightNoAls*BrightnessPref.
extern const char kInternalBacklightMaxNitsPref[];

// Ambient-light-sensor-derived internal backlight brightness steps. See
// powerd/policy/ambient_light_handler.h for details.
extern const char kInternalBacklightAlsStepsPref[];

// Starting internal backlight brightness while on line and battery power for
// systems lacking an ambient light sensor. See
// powerd/policy/internal_backlight_controller.cc for details.
extern const char kInternalBacklightNoAlsAcBrightnessPref[];
extern const char kInternalBacklightNoAlsBatteryBrightnessPref[];

// Ambient-light-sensor-derived keyboard backlight brightness steps.
extern const char kKeyboardBacklightAlsStepsPref[];

// User-requested keyboard backlight brightness steps, given as ascending
// newline-separated percentages in the range [0.0, 100.0].
extern const char kKeyboardBacklightUserStepsPref[];

// Initial brightness for the keyboard backlight for systems that lack ambient
// light sensors, in the range [0.0, 100.0].
extern const char kKeyboardBacklightNoAlsBrightnessPref[];

// Duration in milliseconds the keyboard backlight should remain on after
// hovering stops (on systems that support hover detection).
extern const char kKeyboardBacklightKeepOnAfterHoverMsPref[];

// If true, the system won't be suspended due to user inactivity unless a USB
// input device is connected.
extern const char kRequireUsbInputDeviceToSuspendPref[];

// Milliseconds to wait before polling the power status again.
extern const char kBatteryPollIntervalPref[];

// Milliseconds to wait after boot, line power being connected or disconnected,
// or the system resuming before collecting the battery current to provide
// time-to-empty/full estimates.
extern const char kBatteryStabilizedAfterStartupMsPref[];
extern const char kBatteryStabilizedAfterLinePowerConnectedMsPref[];
extern const char kBatteryStabilizedAfterLinePowerDisconnectedMsPref[];
extern const char kBatteryStabilizedAfterResumeMsPref[];

// Maximum number of current and charge samples that should be averaged.
extern const char kMaxCurrentSamplesPref[];
extern const char kMaxChargeSamplesPref[];

// Minimum maximum power in watts that must be reported by a USB power source in
// order for it to be classified as an AC power source.
extern const char kUsbMinAcWattsPref[];

// Milliseconds to wait after setting the backlight to 0 before asking Chrome to
// turn off the display via DPMS.
extern const char kTurnOffScreenTimeoutMsPref[];

// If true, disables dark resume even on systems where it is available.
extern const char kDisableDarkResumePref[];

// Newline-separated suspend durations to use for dark resume at different
// battery levels, with each line taking the form "<battery-level> <seconds>".
extern const char kDarkResumeSuspendDurationsPref[];

// sysfs device directories with power/dark_resume_active and
// power/dark_resume_source files, respectively, that are used to control
// whether dark resume is enabled in the kernel.
extern const char kDarkResumeDevicesPref[];
extern const char kDarkResumeSourcesPref[];

// If true, policies sent by Chrome will be ignored.
extern const char kIgnoreExternalPolicyPref[];

// Use docked mode when an external display is connected and the lid is closed.
extern const char kAllowDockedModePref[];

// Number of user sessions that have been active on the current charge.
// Written by powerd to persist the count across reboots for metrics-reporting.
extern const char kNumSessionsOnCurrentChargePref[];

// If true, the device has an ambient light sensor.
extern const char kHasAmbientLightSensorPref[];

// If true, the device has a keyboard backlight.
extern const char kHasKeyboardBacklightPref[];

// If true, the device doesn't have an internal display.
extern const char kExternalDisplayOnlyPref[];

// If true, the device has a legacy ACPI power button that doesn't report button
// releases properly.
extern const char kLegacyPowerButtonPref[];

// If true, disable VT switching before suspending.
extern const char kLockVTBeforeSuspendPref[];

// If true, record suspend and resume timestamps in eventlog using the "mosys"
// command.
extern const char kMosysEventlogPref[];

// If true, use /dev/tty0 to query the active VT periodically and avoid treating
// the user as idle while VT2 is active.
extern const char kCheckActiveVTPref[];

// If true, use CRAS, the Chrome OS audio server, to monitor audio activity and
// to mute audio when suspending.
extern const char kUseCrasPref[];

// Integer TPM dictionary-attack counter value at or above which the system will
// suspend instead of shutting down in response to idle or lid-close (see
// http://crbug.com/462428). Set to 0 to disable querying the TPM.
extern const char kTpmCounterSuspendThresholdPref[];

// Time interval between fetches of the TPM status, in seconds.
extern const char kTpmStatusIntervalSecPref[];

// Miscellaneous constants.

// Default directories where read/write and read-only powerd preference files
// are stored.
extern const char kReadWritePrefsDir[];
extern const char kReadOnlyPrefsDir[];

// Subdirectory within the read-only prefs dir where board-specific prefs are
// stored.
extern const char kBoardSpecificPrefsSubdir[];

// sysfs directory containing internal backlight devices and a glob-style
// pattern matching device names.
extern const char kInternalBacklightPath[];
extern const char kInternalBacklightPattern[];

// sysfs directory containing keyboard backlight devices and a glob-style
// pattern matching device names.
extern const char kKeyboardBacklightPath[];
extern const char kKeyboardBacklightPattern[];

// sysfs directory containing information about connected power sources.
extern const char kPowerStatusPath[];

// Small value used when comparing floating-point percentages.
extern const double kEpsilon;

// Total time that should be used to gradually animate the backlight level
// to a new brightness, in milliseconds.  Note that some
// BacklightController implementations may not use animated transitions.
extern const int64_t kFastBacklightTransitionMs;
extern const int64_t kSlowBacklightTransitionMs;

enum PowerSource {
  POWER_AC,
  POWER_BATTERY,
};

enum LidState {
  LID_OPEN,
  LID_CLOSED,
  LID_NOT_PRESENT,
};

enum SessionState {
  SESSION_STOPPED,
  SESSION_STARTED,
};

// Current status of update engine, the system updater.
enum UpdaterState {
  // No update is currently being applied.
  UPDATER_IDLE,
  // An update is being downloaded, verified, or applied.
  UPDATER_UPDATING,
  // An update has been successfully applied and will be used after a reboot.
  UPDATER_UPDATED,
};

enum DisplayMode {
  DISPLAY_NORMAL,
  DISPLAY_PRESENTATION,
};

enum ButtonState {
  BUTTON_UP,
  BUTTON_DOWN,
  BUTTON_REPEAT,
};

// Reasons for the system being shut down.
// Note: These are reported in a histogram and must not be renumbered.
enum ShutdownReason {
  // Explicit user request (e.g. holding power button).
  SHUTDOWN_REASON_USER_REQUEST            = 0,
  // Request from StateController (e.g. lid was closed or user was inactive).
  SHUTDOWN_REASON_STATE_TRANSITION        = 1,
  // Battery level dropped below shutdown threshold.
  SHUTDOWN_REASON_LOW_BATTERY             = 2,
  // Multiple suspend attempts failed.
  SHUTDOWN_REASON_SUSPEND_FAILED          = 3,
  // Battery level was below threshold during dark resume from suspend.
  SHUTDOWN_REASON_DARK_RESUME             = 4,
  // System is being rebooted to apply an update.
  SHUTDOWN_REASON_SYSTEM_UPDATE           = 5,
  // Failed to properly recover from dark resume.
  SHUTDOWN_REASON_EXIT_DARK_RESUME_FAILED = 6,
};

// Returns human-readable descriptions of enum values.
std::string PowerSourceToString(PowerSource source);
std::string LidStateToString(LidState state);
std::string SessionStateToString(SessionState state);
std::string UpdaterStateToString(UpdaterState state);
std::string DisplayModeToString(DisplayMode mode);
std::string ButtonStateToString(ButtonState state);
std::string ShutdownReasonToString(ShutdownReason reason);

}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
