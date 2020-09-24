// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
#define POWER_MANAGER_COMMON_POWER_CONSTANTS_H_

#include <stdint.h>

#include <string>

#include "base/time/time.h"

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

// If true, force special behavior for factory mode (e.g. don't suspend on lid
// close or idle, don't turn the screen off for inactivity, etc.).
extern const char kFactoryModePref[];

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
// hovering stops (on systems that support hover detection) or after the last
// report of user activity (if kKeyboardBacklightTurnOnForUserActivityPref is
// enabled).
extern const char kKeyboardBacklightKeepOnMsPref[];

// Alternate delay used in place of |kKeyboardBacklightKeepOnMsPref| while
// fullscreen video is playing.
extern const char kKeyboardBacklightKeepOnDuringVideoMsPref[];

// If true, powerd will dynamically turn the keyboard backlight on when user
// activity is detected and disable it after kKeyboardBacklightKeepOnMsPref.
// This can be set on systems that lack ambient light sensors and hover
// detection to reduce the backlight's power consumption. It has no effect if
// kDetectHoverPref is set.
extern const char kKeyboardBacklightTurnOnForUserActivityPref[];

// Smoothing constant used to calculated smoothed ambient lux level, in the
// range of (0.0, 1.0]. Value closer to 0.0 means smoothed_lux will respond to
// ambient light change slower. Value of 1.0 means smoothing is disabled.
extern const char kAlsSmoothingConstantPref[];

// If true, the system won't be suspended due to user inactivity unless a USB
// input device is connected.
extern const char kRequireUsbInputDeviceToSuspendPref[];

// Milliseconds to wait before polling the power status again after the number
// of samples is equal to |kMaxCurrentSamplesPref|.
extern const char kBatteryPollIntervalPref[];

// Milliseconds to wait before polling the power status again when the number of
// samples is less than |kMaxCurrentSamplesPref|.
extern const char kBatteryPollIntervalInitialPref[];

// Milliseconds to wait after boot, line power being connected or disconnected,
// or the system resuming before start collecting the battery current to
// provide time-to-empty/full estimates.
extern const char kBatteryStabilizedAfterStartupMsPref[];
extern const char kBatteryStabilizedAfterLinePowerConnectedMsPref[];
extern const char kBatteryStabilizedAfterLinePowerDisconnectedMsPref[];
extern const char kBatteryStabilizedAfterResumeMsPref[];

// If true, multiple battery directories will be read from sysfs if present.
extern const char kMultipleBatteriesPref[];

// Number of current and charge samples that need to be averaged before
// providing time-to-empty/full estimates.
extern const char kMaxCurrentSamplesPref[];
extern const char kMaxChargeSamplesPref[];

// Minimum maximum power in watts that must be reported by a USB power source in
// order for it to be classified as an AC power source.
extern const char kUsbMinAcWattsPref[];

// String describing the position of each charging port on the system. Each line
// contains "NAME ENUM", where NAME is the basename of a sysfs subdirectory
// describing the port (e.g. "CROS_USB_PD_CHARGER0") and ENUM is the name of
// value from the PowerSupplyProperties::PowerSource::Port enum (e.g. "LEFT",
// "RIGHT", "LEFT_FRONT", etc.).
extern const char kChargingPortsPref[];

// Milliseconds to wait after setting the backlight to 0 before asking Chrome to
// turn off the display via DPMS.
extern const char kTurnOffScreenTimeoutMsPref[];

// If true, disables dark resume even on systems where it is available.
extern const char kDisableDarkResumePref[];

// Seconds in suspend without full resume after which the device should shutdown
// proactively. Should be a positive integer for the feature to be enabled.
extern const char kShutdownFromSuspendSecPref[];

// sysfs device directories with power/dark_resume_active and
// power/dark_resume_source files, respectively, that are used to control
// whether dark resume is enabled in the kernel.
extern const char kDarkResumeDevicesPref[];
extern const char kDarkResumeSourcesPref[];

// If true, policies sent by Chrome will be ignored.
extern const char kIgnoreExternalPolicyPref[];

// Number of user sessions that have been active on the current charge.
// Written by powerd to persist the count across reboots for metrics-reporting.
extern const char kNumSessionsOnCurrentChargePref[];

// Number of ambient light sensors on the device.
extern const char kHasAmbientLightSensorPref[];

// If true, device is allowed to have Ambient EQ feature.
extern const char kAllowAmbientEQ[];

// If true, the device has a charge controller responsible for handling
// power policies.
extern const char kHasChargeControllerPref[];

// If true, the device has a keyboard backlight.
extern const char kHasKeyboardBacklightPref[];

// If true, the device doesn't have an internal display.
extern const char kExternalDisplayOnlyPref[];

// If true, the device has a legacy ACPI power button that doesn't report button
// releases properly.
extern const char kLegacyPowerButtonPref[];

// If true, record suspend and resume timestamps in eventlog using the "mosys"
// command.
extern const char kMosysEventlogPref[];

// If true, use CRAS, the Chrome OS audio server, to monitor audio activity and
// to mute audio when suspending.
extern const char kUseCrasPref[];

// Integer TPM dictionary-attack counter value at or above which the system will
// suspend instead of shutting down in response to idle or lid-close (see
// http://crbug.com/462428). Set to 0 to disable querying the TPM.
extern const char kTpmCounterSuspendThresholdPref[];

// Time interval between fetches of the TPM status, in seconds.
extern const char kTpmStatusIntervalSecPref[];

// If true, suspend to idle by writing freeze to /sys/power/state. Otherwise
// suspend by writing mem to /sys/power/state.
extern const char kSuspendToIdlePref[];

// If true, update wifi transmit power when in tablet vs. clamshell mode.
extern const char kSetWifiTransmitPowerForTabletModePref[];

// If true, update wifi transmit power based on proximity SAR sensors.
extern const char kSetWifiTransmitPowerForProximityPref[];

// If true, update wifi transmit power based on proximity activity sensors.
extern const char kSetWifiTransmitPowerForActivityProximityPref[];

// If true, update cellular transmit power when in tablet vs. clamshell mode.
extern const char kSetCellularTransmitPowerForTabletModePref[];

// If true, update cellular transmit power based on proximity SAR sensors.
extern const char kSetCellularTransmitPowerForProximityPref[];

// If true, update cellular transmit power based on proximity activity sensors.
extern const char kSetCellularTransmitPowerForActivityProximityPref[];

// GPIO number for the dynamic power reduction signal of a built-in cellular
// modem.
extern const char kSetCellularTransmitPowerDprGpioPref[];

// If true, enables console during suspend.
extern const char kEnableConsoleDuringSuspendPref[];

// Maximum time in milliseconds to wait to resuspend (i.e. wait for all suspend
// delays) after a dark resume.
extern const char kMaxDarkSuspendDelayTimeoutMsPref[];

// Mode for system suspend. Valid values are "s2idle", "shallow" and "deep".
// Please look at https://www.kernel.org/doc/Documentation/power/states.txt for
// more information.
extern const char kSuspendModePref[];

// If true, enables wake from suspend (S3/S0ix) on DP hot plug detect on type-c
// ports.
extern const char kWakeOnDpPref[];

// Time in hours for Smart Discharge to help device last through.
extern const char kSmartDischargeToZeroHrPref[];

// Current in microamps that the device is expected to consume in battery cutoff
// state. This field allows EC to calculate the battery level at which the
// device should enter battery cutoff state.
extern const char kCutoffPowerUaPref[];

// Current in microamps that the device is expected to consume in hibernate
// state. This field allows EC to calculate the battery level at which the
// device should enter S5 or hibernate if EC can wake up by timer.
extern const char kHibernatePowerUaPref[];

// Miscellaneous constants.

// Name of the cros_fp fingerprint sensor input device.
extern const char kCrosFpInputDevName[];

// sysfs directory containing internal backlight devices and a glob-style
// pattern matching device names.
extern const char kInternalBacklightPath[];
extern const char kInternalBacklightPattern[];

// sysfs directory containing keyboard backlight devices and a glob-style
// pattern matching device names.
extern const char kKeyboardBacklightPath[];
extern const char kKeyboardBacklightPattern[];

// udev subsystem used to announce changes to keyboard backlights.
extern const char kKeyboardBacklightUdevSubsystem[];

// sysfs directory containing information about connected power sources.
extern const char kPowerStatusPath[];

// sysfs path to the wakeup control file relative to the device sysfs
// directory (power/wakeup).
extern const char kPowerWakeup[];

// Program used to run code as root.
extern const char kSetuidHelperPath[];

// Information about "NameOwnerChanged" D-Bus signals emitted by dbus-daemon.
extern const char kBusServiceName[];
extern const char kBusServicePath[];
extern const char kBusInterface[];
extern const char kBusNameOwnerChangedSignal[];

// Small value used when comparing floating-point percentages.
extern const double kEpsilon;

// Total time that should be used to gradually animate the backlight level
// to a new brightness, in milliseconds.  Note that some
// BacklightController implementations may not use animated transitions.
extern const int64_t kFastBacklightTransitionMs;
extern const int64_t kSlowBacklightTransitionMs;
// udev subsystem to watch for input device related events.
extern const char kInputUdevSubsystem[];

enum class PowerSource {
  AC,
  BATTERY,
};

enum class LidState {
  OPEN,
  CLOSED,
  NOT_PRESENT,
};

enum class RadioTransmitPower {
  LOW,
#if USE_TROGDOR_SAR_HACK
  MEDIUM,
#endif  // USE_TROGDOR_SAR_HACK
  HIGH,
  UNSPECIFIED,
};

#if USE_TROGDOR_SAR_HACK
enum class ModemState {
  OFFLINE,
  ONLINE,
  UNKNOWN,
};
#endif  // USE_TROGDOR_SAR_HACK

// Convertible Chromebooks may either be folded into a tablet or used as a
// clamshell.
enum class TabletMode {
  ON,
  OFF,
  UNSUPPORTED,
};

// Chromebooks may have one or more sensors that are able to indicate
// the user's physical proximity to the device.
enum class UserProximity {
  NEAR,
  FAR,
  UNKNOWN,
};

enum class SessionState {
  STOPPED,
  STARTED,
};

enum class DisplayMode {
  NORMAL,
  PRESENTATION,
};

enum class ButtonState {
  UP,
  DOWN,
  REPEAT,
};

// Reasons for the system being shut down or rebooted.
// Note: These are reported in a histogram and must not be renumbered.
enum class ShutdownReason {
  // Explicit user request (e.g. holding power button).
  USER_REQUEST = 0,
  // Request from StateController (e.g. lid was closed or user was inactive).
  STATE_TRANSITION = 1,
  // Battery level dropped below shutdown threshold.
  LOW_BATTERY = 2,
  // Multiple suspend attempts failed.
  SUSPEND_FAILED = 3,
  // Device spent |kShutdownFromSuspendAfterSecPref| in suspend without full
  // resume.
  SHUTDOWN_FROM_SUSPEND = 4,
  // System is being rebooted to apply an update.
  SYSTEM_UPDATE = 5,
  // Unclassified external request sent to powerd by another process.
  OTHER_REQUEST_TO_POWERD = 7,
};

enum class WifiRegDomain {
  FCC,
  EU,
  REST_OF_WORLD,
  NONE,
};

// Returns human-readable descriptions of enum values.
std::string PowerSourceToString(PowerSource source);
std::string LidStateToString(LidState state);
std::string TabletModeToString(TabletMode mode);
std::string UserProximityToString(UserProximity proximity);
std::string SessionStateToString(SessionState state);
std::string DisplayModeToString(DisplayMode mode);
std::string ButtonStateToString(ButtonState state);
std::string ShutdownReasonToString(ShutdownReason reason);
std::string WifiRegDomainToString(WifiRegDomain domain);

}  // namespace power_manager

#endif  // POWER_MANAGER_COMMON_POWER_CONSTANTS_H_
