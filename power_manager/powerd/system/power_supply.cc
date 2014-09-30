// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/power_supply.h"

#include <algorithm>
#include <cmath>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace system {

namespace {

// sysfs reports only integer values.  For non-integral values, it scales them
// up by 10^6.  This factor scales them back down accordingly.
const double kDoubleScaleFactor = 0.000001;

// Default time interval between polls, in milliseconds.
const int kDefaultPollMs = 30000;

// Default values for |battery_stabilized_after_*_delay_|, in milliseconds.
const int kDefaultBatteryStabilizedAfterStartupDelayMs = 5000;
const int kDefaultBatteryStabilizedAfterLinePowerConnectedDelayMs = 5000;
const int kDefaultBatteryStabilizedAfterLinePowerDisconnectedDelayMs = 5000;
const int kDefaultBatteryStabilizedAfterResumeDelayMs = 5000;

// Different power supply types reported by the kernel.
const char kBatteryType[] = "Battery";
const char kMainsType[] = "Mains";

// Battery states reported by the kernel. This is not the full set of
// possible states; see drivers/power/power_supply_sysfs.c.
const char kBatteryStatusCharging[] = "Charging";
const char kBatteryStatusFull[] = "Full";

// Reads the contents of |filename| within |directory| into |out|, trimming
// trailing whitespace.  Returns true on success.
bool ReadAndTrimString(const base::FilePath& directory,
                       const std::string& filename,
                       std::string* out) {
  if (!base::ReadFileToString(directory.Append(filename), out))
    return false;

  base::TrimWhitespaceASCII(*out, base::TRIM_TRAILING, out);
  return true;
}

// Reads a 64-bit integer value from a file and returns true on success.
bool ReadInt64(const base::FilePath& directory,
               const std::string& filename,
               int64_t* out) {
  std::string buffer;
  if (!ReadAndTrimString(directory, filename, &buffer))
    return false;
  return base::StringToInt64(buffer, out);
}

// Reads an integer value and scales it to a double (see |kDoubleScaleFactor|.
// Returns 0.0 on failure.
double ReadScaledDouble(const base::FilePath& directory,
                        const std::string& filename) {
  int64_t value = 0;
  return ReadInt64(directory, filename, &value) ?
      kDoubleScaleFactor * value : 0.0;
}

// Returns true if |type|, a power supply type read from a "type" file in
// sysfs, indicates USB.
bool IsUsbType(const std::string& type) {
  // These are defined in drivers/power/power_supply_sysfs.c in the kernel.
  return type == "USB" || type == "USB_DCP" || type == "USB_CDP" ||
      type == "USB_ACA";
}

// Returns true if |model_name|, a line power source's model, indicates that an
// original spring AC charger is connected. Also assumes that an original spring
// AC charger is connected if the firmware is outdated and doesn't report the
// model.
bool IsOriginalSpringCharger(const std::string& model_name) {
  return model_name == PowerSupply::kOriginalSpringChargerModelName ||
      model_name == PowerSupply::kOldFirmwareModelName;
}

}  // namespace

base::TimeTicks PowerSupply::TestApi::GetCurrentTime() const {
  return power_supply_->clock_->GetCurrentTime();
}

void PowerSupply::TestApi::SetCurrentTime(base::TimeTicks now) {
  power_supply_->clock_->set_current_time_for_testing(now);
}

void PowerSupply::TestApi::AdvanceTime(base::TimeDelta interval) {
  power_supply_->clock_->set_current_time_for_testing(
      GetCurrentTime() + interval);
}

bool PowerSupply::TestApi::TriggerPollTimeout() {
  if (!power_supply_->poll_timer_.IsRunning())
    return false;

  power_supply_->poll_timer_.Stop();
  power_supply_->HandlePollTimeout();
  return true;
}

const char PowerSupply::kUdevSubsystem[] = "power_supply";
const int PowerSupply::kObservedBatteryChargeRateMinMs = kDefaultPollMs;
const int PowerSupply::kBatteryStabilizedSlackMs = 50;
const double PowerSupply::kLowBatteryShutdownSafetyPercent = 5.0;
const char PowerSupply::kOriginalSpringChargerModelName[] = "0x17";
const char PowerSupply::kOldFirmwareModelName[] = "0x00";

PowerSupply::PowerSupply()
    : prefs_(NULL),
      udev_(NULL),
      clock_(new Clock),
      power_status_initialized_(false),
      low_battery_shutdown_percent_(0.0),
      is_suspended_(false),
      full_factor_(1.0) {
}

PowerSupply::~PowerSupply() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kUdevSubsystem, this);
}

void PowerSupply::Init(const base::FilePath& power_supply_path,
                       PrefsInterface* prefs,
                       UdevInterface* udev) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kUdevSubsystem, this);

  prefs_ = prefs;
  power_supply_path_ = power_supply_path;
  GetPowerSupplyPaths();

  poll_delay_ = GetMsPref(kBatteryPollIntervalPref, kDefaultPollMs);
  battery_stabilized_after_startup_delay_ = GetMsPref(
      kBatteryStabilizedAfterStartupMsPref,
      kDefaultBatteryStabilizedAfterStartupDelayMs);
  battery_stabilized_after_line_power_connected_delay_ = GetMsPref(
      kBatteryStabilizedAfterLinePowerConnectedMsPref,
      kDefaultBatteryStabilizedAfterLinePowerConnectedDelayMs);
  battery_stabilized_after_line_power_disconnected_delay_ = GetMsPref(
      kBatteryStabilizedAfterLinePowerDisconnectedMsPref,
      kDefaultBatteryStabilizedAfterLinePowerDisconnectedDelayMs);
  battery_stabilized_after_resume_delay_ = GetMsPref(
      kBatteryStabilizedAfterResumeMsPref,
      kDefaultBatteryStabilizedAfterResumeDelayMs);

  prefs_->GetDouble(kPowerSupplyFullFactorPref, &full_factor_);
  full_factor_ = std::min(std::max(kEpsilon, full_factor_), 1.0);

  int64_t shutdown_time_sec = 0;
  if (prefs_->GetInt64(kLowBatteryShutdownTimePref, &shutdown_time_sec)) {
    low_battery_shutdown_time_ =
        base::TimeDelta::FromSeconds(shutdown_time_sec);
  }

  // The percentage-based threshold takes precedence over the time-based
  // threshold.
  if (prefs_->GetDouble(kLowBatteryShutdownPercentPref,
                        &low_battery_shutdown_percent_)) {
    low_battery_shutdown_time_ = base::TimeDelta();
  }

  int64_t samples = 0;
  CHECK(prefs_->GetInt64(kMaxCurrentSamplesPref, &samples));
  current_samples_on_line_power_.reset(new RollingAverage(samples));
  current_samples_on_battery_power_.reset(new RollingAverage(samples));

  CHECK(prefs_->GetInt64(kMaxChargeSamplesPref, &samples));
  charge_samples_.reset(new RollingAverage(samples));

  // This log message is needed by the power_LoadTest autotest.
  LOG(INFO) << "Using low battery time threshold of "
            << low_battery_shutdown_time_.InSeconds()
            << " secs and using low battery percent threshold of "
            << low_battery_shutdown_percent_;

  DeferBatterySampling(battery_stabilized_after_startup_delay_);
  SchedulePoll();
}

void PowerSupply::AddObserver(PowerSupplyObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void PowerSupply::RemoveObserver(PowerSupplyObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

PowerStatus PowerSupply::GetPowerStatus() const {
  return power_status_;
}

bool PowerSupply::RefreshImmediately() {
  const bool success = UpdatePowerStatus();
  if (!is_suspended_)
    SchedulePoll();
  if (success) {
    notify_observers_task_.Reset(
        base::Bind(&PowerSupply::NotifyObservers, base::Unretained(this)));
    base::MessageLoop::current()->PostTask(
        FROM_HERE, notify_observers_task_.callback());
  }
  return success;
}

void PowerSupply::SetSuspended(bool suspended) {
  if (is_suspended_ == suspended)
    return;

  is_suspended_ = suspended;
  if (is_suspended_) {
    VLOG(1) << "Stopping polling due to suspend";
    poll_timer_.Stop();
    current_poll_delay_for_testing_ = base::TimeDelta();
  } else {
    DeferBatterySampling(battery_stabilized_after_resume_delay_);
    charge_samples_->Clear();
    current_samples_on_line_power_->Clear();
    RefreshImmediately();
  }
}

void PowerSupply::OnUdevEvent(const std::string& subsystem,
                              const std::string& sysname,
                              UdevAction action) {
  VLOG(1) << "Heard about udev event";
  if (!is_suspended_)
    RefreshImmediately();
}

base::TimeDelta PowerSupply::GetMsPref(const std::string& pref_name,
                                       int64_t default_duration_ms) const {
  prefs_->GetInt64(pref_name, &default_duration_ms);
  return base::TimeDelta::FromMilliseconds(default_duration_ms);
}

void PowerSupply::DeferBatterySampling(base::TimeDelta stabilized_delay) {
  const base::TimeTicks now = clock_->GetCurrentTime();
  battery_stabilized_timestamp_ =
      std::max(battery_stabilized_timestamp_, now + stabilized_delay);
  VLOG(1) << "Waiting "
          << (battery_stabilized_timestamp_ - now).InMilliseconds()
          << " ms for battery current and charge to stabilize";
}

bool PowerSupply::UpdatePowerStatus() {
  VLOG(1) << "Updating power status";
  PowerStatus status;

  if (battery_path_.empty() || line_power_path_.empty())
    GetPowerSupplyPaths();

  status.line_power_path = line_power_path_.value();
  status.battery_path = battery_path_.value();

  std::string battery_status_string;
  if (base::PathExists(battery_path_)) {
    int64_t present_value = 0;
    ReadInt64(battery_path_, "present", &present_value);
    status.battery_is_present = present_value != 0;
    if (status.battery_is_present)
      ReadAndTrimString(battery_path_, "status", &battery_status_string);
  } else {
    status.battery_is_present = false;
  }

  if (base::PathExists(line_power_path_)) {
    int64_t online_value = 0;
    ReadInt64(line_power_path_, "online", &online_value);
    status.line_power_on = online_value != 0;
    ReadAndTrimString(line_power_path_, "type", &status.line_power_type);
    ReadAndTrimString(line_power_path_, "model_name",
                      &status.line_power_model_name);

    if (status.line_power_on) {
      status.external_power = IsUsbType(status.line_power_type) ?
          PowerSupplyProperties_ExternalPower_USB :
          (IsOriginalSpringCharger(status.line_power_model_name) ?
           PowerSupplyProperties_ExternalPower_ORIGINAL_SPRING_CHARGER :
           PowerSupplyProperties_ExternalPower_AC);
      status.line_power_voltage =
          ReadScaledDouble(line_power_path_, "voltage_now");
      status.line_power_current =
          ReadScaledDouble(line_power_path_, "current_now");
    } else {
      status.external_power = PowerSupplyProperties_ExternalPower_DISCONNECTED;
    }
  } else if (!status.battery_is_present) {
    // If there's no battery, assume that the system is on AC power.
    status.line_power_on = true;
    status.line_power_type = kMainsType;
    status.external_power = PowerSupplyProperties_ExternalPower_AC;
  } else {
    // Otherwise, infer the external power source from the kernel-reported
    // battery status.
    status.line_power_on =
        battery_status_string == kBatteryStatusCharging ||
        battery_status_string == kBatteryStatusFull;
    status.external_power = status.line_power_on ?
        PowerSupplyProperties_ExternalPower_AC :
        PowerSupplyProperties_ExternalPower_DISCONNECTED;
  }

  // The rest of the calculations all require a battery.
  if (!status.battery_is_present) {
    status.battery_state = PowerSupplyProperties_BatteryState_NOT_PRESENT;
    power_status_ = status;
    power_status_initialized_ = true;
    return true;
  }

  // POWER_SUPPLY_PROP_VENDOR does not seem to be a valid property
  // defined in <linux/power_supply.h>.
  if (base::PathExists(battery_path_.Append("manufacturer")))
    ReadAndTrimString(battery_path_, "manufacturer", &status.battery_vendor);
  else
    ReadAndTrimString(battery_path_, "vendor", &status.battery_vendor);
  ReadAndTrimString(battery_path_, "model_name", &status.battery_model_name);
  ReadAndTrimString(battery_path_, "serial_number", &status.battery_serial);
  ReadAndTrimString(battery_path_, "technology", &status.battery_technology);

  double battery_voltage = ReadScaledDouble(battery_path_, "voltage_now");
  status.battery_voltage = battery_voltage;

  // Attempt to determine nominal voltage for time remaining calculations.
  // The battery voltage used in calculating time remaining.  This may or may
  // not be the same as the instantaneous voltage |battery_voltage|, as voltage
  // levels vary over the time the battery is charged or discharged.
  double nominal_voltage = 0.0;
  if (base::PathExists(battery_path_.Append("voltage_min_design")))
    nominal_voltage = ReadScaledDouble(battery_path_, "voltage_min_design");
  else if (base::PathExists(battery_path_.Append("voltage_max_design")))
    nominal_voltage = ReadScaledDouble(battery_path_, "voltage_max_design");

  // Nominal voltage is not required to obtain charge level.  If it is missing,
  // just log a message, set to |battery_voltage| so time remaining
  // calculations will function, and proceed.
  if (nominal_voltage <= 0) {
    LOG(WARNING) << "Invalid voltage_min/max_design reading: "
                 << nominal_voltage << "V."
                 << " Time remaining calculations will not be available.";
    nominal_voltage = battery_voltage;
  }
  status.nominal_voltage = nominal_voltage;

  // ACPI has two different battery types: charge_battery and energy_battery.
  // The main difference is that charge_battery type exposes
  // 1. current_now in A
  // 2. charge_{now, full, full_design} in Ah
  // while energy_battery type exposes
  // 1. power_now W
  // 2. energy_{now, full, full_design} in Wh
  // Change all the energy readings to charge format.
  // If both energy and charge reading are present (some non-ACPI drivers
  // expose both readings), read only the charge format.
  double battery_charge_full = 0;
  double battery_charge_full_design = 0;
  double battery_charge = 0;

  if (base::PathExists(battery_path_.Append("charge_full"))) {
    battery_charge_full = ReadScaledDouble(battery_path_, "charge_full");
    battery_charge_full_design =
        ReadScaledDouble(battery_path_, "charge_full_design");
    battery_charge = ReadScaledDouble(battery_path_, "charge_now");
  } else if (base::PathExists(battery_path_.Append("energy_full"))) {
    // Valid |battery_voltage| is required to determine the charge so return
    // early if it is not present. In this case, we know nothing about
    // battery state or remaining percentage, so set proper status.
    if (battery_voltage <= 0) {
      LOG(WARNING) << "Invalid voltage_now reading for energy-to-charge"
                   << " conversion: " << battery_voltage;
      return false;
    }
    battery_charge_full =
        ReadScaledDouble(battery_path_, "energy_full") / battery_voltage;
    battery_charge_full_design =
        ReadScaledDouble(battery_path_, "energy_full_design") / battery_voltage;
    battery_charge =
        ReadScaledDouble(battery_path_, "energy_now") / battery_voltage;
  } else {
    LOG(WARNING) << "No charge/energy readings for battery";
    return false;
  }

  status.battery_charge_full = battery_charge_full;
  status.battery_charge_full_design = battery_charge_full_design;
  status.battery_charge = battery_charge;
  if (status.battery_charge_full <= 0.0) {
    LOG(WARNING) << "Got battery-full charge of " << status.battery_charge_full;
    return false;
  }

  // The current can be reported as negative on some systems but not on
  // others, so it can't be used to determine whether the battery is
  // charging or discharging.
  double battery_current = 0.0;
  if (base::PathExists(battery_path_.Append("power_now"))) {
    battery_current = fabs(ReadScaledDouble(battery_path_, "power_now")) /
        battery_voltage;
  } else {
    battery_current = fabs(ReadScaledDouble(battery_path_, "current_now"));
  }
  status.battery_current = battery_current;

  status.battery_energy = battery_charge * nominal_voltage;
  status.battery_energy_rate = battery_current * battery_voltage;

  status.battery_percentage = util::ClampPercent(
      100.0 * battery_charge / battery_charge_full);
  status.display_battery_percentage = util::ClampPercent(
      100.0 * (status.battery_percentage - low_battery_shutdown_percent_) /
      (100.0 * full_factor_ - low_battery_shutdown_percent_));

  const bool battery_is_full =
      battery_charge >= battery_charge_full * full_factor_;

  if (status.line_power_on) {
    if (battery_is_full) {
      status.battery_state = PowerSupplyProperties_BatteryState_FULL;
    } else if (battery_current > 0.0 &&
               (battery_status_string == kBatteryStatusCharging ||
                battery_status_string == kBatteryStatusFull)) {
      status.battery_state = PowerSupplyProperties_BatteryState_CHARGING;
    } else {
      status.battery_state = PowerSupplyProperties_BatteryState_DISCHARGING;
    }
  } else {
    status.battery_state = PowerSupplyProperties_BatteryState_DISCHARGING;
  }

  if (power_status_initialized_ &&
      status.line_power_on != power_status_.line_power_on) {
    DeferBatterySampling(status.line_power_on ?
        battery_stabilized_after_line_power_connected_delay_ :
        battery_stabilized_after_line_power_disconnected_delay_);
    charge_samples_->Clear();

    // Chargers can deliver highly-variable currents depending on various
    // factors (e.g. negotiated current for USB chargers, charge level, etc.).
    // If one was just connected, throw away the previous average.
    if (status.line_power_on)
      current_samples_on_line_power_->Clear();
  }

  base::TimeTicks now = clock_->GetCurrentTime();
  if (now >= battery_stabilized_timestamp_) {
    charge_samples_->AddSample(battery_charge, now);

    if (battery_current > 0.0) {
      const double signed_current =
          (status.battery_state ==
           PowerSupplyProperties_BatteryState_DISCHARGING) ?
          -battery_current : battery_current;
      if (status.line_power_on)
        current_samples_on_line_power_->AddSample(signed_current, now);
      else
        current_samples_on_battery_power_->AddSample(signed_current, now);
    }
  }

  UpdateObservedBatteryChargeRate(&status);
  status.is_calculating_battery_time = !UpdateBatteryTimeEstimates(&status);
  status.battery_below_shutdown_threshold =
      IsBatteryBelowShutdownThreshold(status);

  power_status_ = status;
  power_status_initialized_ = true;
  return true;
}

void PowerSupply::GetPowerSupplyPaths() {
  // First check if both line power and battery paths have been found and still
  // exist.  If so, there is no need to do anything else.
  if (base::PathExists(battery_path_) && base::PathExists(line_power_path_))
    return;
  // Use a FileEnumerator to browse through all files/subdirectories in the
  // power supply sysfs directory.
  base::FileEnumerator file_enum(
      power_supply_path_, false, base::FileEnumerator::DIRECTORIES);
  // Read type info from all power sources, and try to identify battery and line
  // power sources.  Their paths are to be stored locally.
  for (base::FilePath path = file_enum.Next();
       !path.empty();
       path = file_enum.Next()) {
    // External devices have "scope" attributes containing the value "Device".
    // Skip them.
    base::FilePath scope_path = path.Append("scope");
    if (base::PathExists(scope_path)) {
      std::string buf;
      base::ReadFileToString(scope_path, &buf);
      base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
      if (buf == "Device") {
        VLOG(1) << "Skipping Power supply " << path.value()
                << " with scope: " << buf;
        continue;
      }
    }
    std::string buf;
    if (base::ReadFileToString(path.Append("type"), &buf)) {
      base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
      // Only look for battery / line power paths if they haven't been found
      // already.  This makes the assumption that they don't change (but battery
      // path can disappear if removed).  So this code should only be run once
      // for each power source.
      if (buf == kBatteryType && battery_path_.empty()) {
        VLOG(1) << "Battery path found: " << path.value();
        battery_path_ = path;
      } else if (buf != kBatteryType && line_power_path_.empty()) {
        VLOG(1) << "Line power path found: " << path.value();
        line_power_path_ = path;
      }
    }
  }
}

bool PowerSupply::UpdateBatteryTimeEstimates(PowerStatus* status) {
  DCHECK(status);
  status->battery_time_to_full = base::TimeDelta();
  status->battery_time_to_empty = base::TimeDelta();
  status->battery_time_to_shutdown = base::TimeDelta();

  if (clock_->GetCurrentTime() < battery_stabilized_timestamp_)
    return false;

  // Positive if the battery is charging and negative if it's discharging.
  const double signed_current = status->line_power_on ?
      current_samples_on_line_power_->GetAverage() :
      current_samples_on_battery_power_->GetAverage();

  switch (status->battery_state) {
    case PowerSupplyProperties_BatteryState_CHARGING:
      if (signed_current <= kEpsilon) {
        status->battery_time_to_full = base::TimeDelta::FromSeconds(-1);
      } else {
        const double charge_to_full = std::max(0.0,
            status->battery_charge_full * full_factor_ -
            status->battery_charge);
        status->battery_time_to_full = base::TimeDelta::FromSeconds(
            roundl(3600 * charge_to_full / signed_current));
      }
      break;
    case PowerSupplyProperties_BatteryState_DISCHARGING:
      if (signed_current >= -kEpsilon) {
        status->battery_time_to_empty = base::TimeDelta::FromSeconds(-1);
        status->battery_time_to_shutdown = base::TimeDelta::FromSeconds(-1);
      } else {
        status->battery_time_to_empty = base::TimeDelta::FromSeconds(
            roundl(3600 * (status->battery_charge * status->nominal_voltage) /
                   (-signed_current * status->battery_voltage)));

        const double shutdown_charge =
            status->battery_charge_full * low_battery_shutdown_percent_ / 100.0;
        const double available_charge = std::max(0.0,
            status->battery_charge - shutdown_charge);
        status->battery_time_to_shutdown = base::TimeDelta::FromSeconds(
            roundl(3600 * (available_charge * status->nominal_voltage) /
                   (-signed_current * status->battery_voltage))) -
            low_battery_shutdown_time_;
        status->battery_time_to_shutdown =
            std::max(base::TimeDelta(), status->battery_time_to_shutdown);
      }
      break;
    case PowerSupplyProperties_BatteryState_FULL:
      break;
    default:
      NOTREACHED() << "Unhandled battery state " << status->battery_state;
  }

  return true;
}

void PowerSupply::UpdateObservedBatteryChargeRate(PowerStatus* status) const {
  DCHECK(status);
  const base::TimeDelta time_delta = charge_samples_->GetTimeDelta();
  status->observed_battery_charge_rate =
      (time_delta.InMilliseconds() < kObservedBatteryChargeRateMinMs) ? 0.0 :
      charge_samples_->GetValueDelta() / (time_delta.InSecondsF() / 3600);
}

bool PowerSupply::IsBatteryBelowShutdownThreshold(
    const PowerStatus& status) const {
  if (low_battery_shutdown_time_ == base::TimeDelta() &&
      low_battery_shutdown_percent_ <= kEpsilon)
    return false;

  // TODO(derat): Figure out what's causing http://crosbug.com/38912.
  if (status.battery_percentage <= kEpsilon) {
    LOG(WARNING) << "Ignoring probably-bogus zero battery percentage";
    return false;
  }

  const bool below_threshold =
      (status.battery_time_to_empty > base::TimeDelta() &&
       status.battery_time_to_empty <= low_battery_shutdown_time_ &&
       status.battery_percentage <= kLowBatteryShutdownSafetyPercent) ||
      status.battery_percentage <= low_battery_shutdown_percent_;

  // Most AC chargers can deliver enough current to prevent the battery from
  // discharging while the device is in use; other chargers (e.g. USB) may not
  // be able to, though. The observed charge rate is checked to verify whether
  // the battery's charge is increasing or decreasing.
  if (status.line_power_on)
    return below_threshold && status.observed_battery_charge_rate < 0.0;

  return below_threshold;
}

void PowerSupply::SchedulePoll() {
  base::TimeDelta delay = poll_delay_;
  base::TimeTicks now = clock_->GetCurrentTime();
  if (battery_stabilized_timestamp_ > now) {
    delay = std::min(delay,
        battery_stabilized_timestamp_ - now +
        base::TimeDelta::FromMilliseconds(kBatteryStabilizedSlackMs));
  }

  VLOG(1) << "Scheduling update in " << delay.InMilliseconds() << " ms";
  poll_timer_.Start(FROM_HERE, delay, this, &PowerSupply::HandlePollTimeout);
  current_poll_delay_for_testing_ = delay;
}

void PowerSupply::HandlePollTimeout() {
  current_poll_delay_for_testing_ = base::TimeDelta();
  const bool success = UpdatePowerStatus();
  SchedulePoll();
  if (success)
    NotifyObservers();
}

void PowerSupply::NotifyObservers() {
  FOR_EACH_OBSERVER(PowerSupplyObserver, observers_, OnPowerStatusUpdate());
}

}  // namespace system
}  // namespace power_manager
