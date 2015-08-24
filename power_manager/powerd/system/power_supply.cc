// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/power_supply.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/clock.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/proto_bindings/power_supply_properties.pb.h"

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
const char kUnknownType[] = "Unknown";

// Battery states reported by the kernel. This is not the full set of
// possible states; see drivers/power/power_supply_sysfs.c.
const char kBatteryStatusCharging[] = "Charging";
const char kBatteryStatusFull[] = "Full";

// Line power status reported by the kernel for a bidirectional port through
// which the system is being charged.
const char kLinePowerStatusCharging[] = "Charging";

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
bool IsUsbChargerType(const std::string& type) {
  // These are defined in drivers/power/power_supply_sysfs.c in the kernel.
  return type == "USB" || type == "USB_DCP" || type == "USB_CDP" ||
      type == "USB_ACA";
}

// Returns true if |path|, a sysfs directory, corresponds to an external
// peripheral (e.g. a wireless mouse or keyboard).
bool IsExternalPeripheral(const base::FilePath& path) {
  std::string scope;
  return ReadAndTrimString(path, "scope", &scope) && scope == "Device";
}

// Returns true if |path|, a sysfs directory, corresponds to a battery.
bool IsBatteryPresent(const base::FilePath& path) {
  int64_t present = 0;
  return ReadInt64(path, "present", &present) && present != 0;
}

// Returns a string describing |type|.
const char* ExternalPowerToString(PowerSupplyProperties::ExternalPower type) {
  switch (type) {
    case PowerSupplyProperties_ExternalPower_AC:
      return "AC";
    case PowerSupplyProperties_ExternalPower_USB:
      return "USB";
    case PowerSupplyProperties_ExternalPower_DISCONNECTED:
      return "none";
  }
  return "unknown";
}

// Less-than comparator for PowerStatus::Source structs.
struct SourceComparator {
  bool operator()(const PowerStatus::Source& a, const PowerStatus::Source& b) {
    return a.id < b.id;
  }
};

// Maps names from PowerSupplyProperties::PowerSource::Port to the corresponding
// values.
PowerSupplyProperties::PowerSource::Port GetPortFromString(
    const std::string& name) {
  if (name == "LEFT")
    return PowerSupplyProperties_PowerSource_Port_LEFT;
  else if (name == "RIGHT")
    return PowerSupplyProperties_PowerSource_Port_RIGHT;
  else if (name == "BACK")
    return PowerSupplyProperties_PowerSource_Port_BACK;
  else if (name == "FRONT")
    return PowerSupplyProperties_PowerSource_Port_FRONT;
  else if (name == "LEFT_FRONT")
    return PowerSupplyProperties_PowerSource_Port_LEFT_FRONT;
  else if (name == "LEFT_BACK")
    return PowerSupplyProperties_PowerSource_Port_LEFT_BACK;
  else if (name == "RIGHT_FRONT")
    return PowerSupplyProperties_PowerSource_Port_RIGHT_FRONT;
  else if (name == "RIGHT_BACK")
    return PowerSupplyProperties_PowerSource_Port_RIGHT_BACK;
  else if (name == "BACK_LEFT")
    return PowerSupplyProperties_PowerSource_Port_BACK_LEFT;
  else if (name == "BACK_RIGHT")
    return PowerSupplyProperties_PowerSource_Port_BACK_RIGHT;
  else
    return PowerSupplyProperties_PowerSource_Port_UNKNOWN;
}

}  // namespace

void CopyPowerStatusToProtocolBuffer(const PowerStatus& status,
                                     PowerSupplyProperties* proto) {
  DCHECK(proto);
  proto->Clear();
  proto->set_external_power(status.external_power);
  proto->set_battery_state(status.battery_state);
  proto->set_battery_percent(status.display_battery_percentage);
  proto->set_supports_dual_role_devices(status.supports_dual_role_devices);

  // Show the user the time until powerd will shut down the system automatically
  // rather than the time until the battery is completely empty.
  proto->set_battery_time_to_empty_sec(
      status.battery_time_to_shutdown.InSeconds());
  proto->set_battery_time_to_full_sec(
      status.battery_time_to_full.InSeconds());
  proto->set_is_calculating_battery_time(status.is_calculating_battery_time);

  if (status.battery_state == PowerSupplyProperties_BatteryState_FULL ||
      status.battery_state == PowerSupplyProperties_BatteryState_CHARGING) {
    proto->set_battery_discharge_rate(-status.battery_energy_rate);
  } else {
    proto->set_battery_discharge_rate(status.battery_energy_rate);
  }

  for (auto source : status.available_external_power_sources) {
    PowerSupplyProperties::PowerSource* proto_source =
        proto->add_available_external_power_source();
    proto_source->set_id(source.id);
    proto_source->set_port(source.port);
    proto_source->set_manufacturer_id(source.manufacturer_id);
    proto_source->set_model_id(source.model_id);
    proto_source->set_max_power(source.max_power);
    proto_source->set_active_by_default(source.active_by_default);
  }
  if (!status.external_power_source_id.empty())
    proto->set_external_power_source_id(status.external_power_source_id);
}

std::string GetPowerStatusBatteryDebugString(const PowerStatus& status) {
  if (!status.battery_is_present)
    return std::string();

  std::string output;
  switch (status.external_power) {
    case PowerSupplyProperties_ExternalPower_AC:
    case PowerSupplyProperties_ExternalPower_USB:
      output = base::StringPrintf("On %s (%s",
          ExternalPowerToString(status.external_power),
          status.line_power_type.c_str());
      if (status.line_power_current || status.line_power_voltage) {
        output += base::StringPrintf(", %.3fA at %.1fV",
            status.line_power_current, status.line_power_voltage);
        if (status.line_power_max_current && status.line_power_max_voltage) {
          output += base::StringPrintf(", max %.1fA at %.1fV",
              status.line_power_max_current, status.line_power_max_voltage);
        }
      }
      output += ") with battery at ";
      break;
    case PowerSupplyProperties_ExternalPower_DISCONNECTED:
      output = "On battery at ";
      break;
  }

  int rounded_actual = lround(status.battery_percentage);
  int rounded_display = lround(status.display_battery_percentage);
  output += base::StringPrintf("%d%%", rounded_actual);
  if (rounded_actual != rounded_display)
    output += base::StringPrintf(" (displayed as %d%%)", rounded_display);
  output += base::StringPrintf(", %.3f/%.3fAh at %.3fA", status.battery_charge,
      status.battery_charge_full, status.battery_current);

  switch (status.battery_state) {
    case PowerSupplyProperties_BatteryState_FULL:
      output += ", full";
      break;
    case PowerSupplyProperties_BatteryState_CHARGING:
      if (status.battery_time_to_full >= base::TimeDelta()) {
        output += ", " + util::TimeDeltaToString(status.battery_time_to_full) +
            " until full";
        if (status.is_calculating_battery_time)
          output += " (calculating)";
      }
      break;
    case PowerSupplyProperties_BatteryState_DISCHARGING:
      if (status.battery_time_to_empty >= base::TimeDelta()) {
        output += ", " + util::TimeDeltaToString(status.battery_time_to_empty) +
            " until empty";
        if (status.is_calculating_battery_time) {
          output += " (calculating)";
        } else if (status.battery_time_to_shutdown !=
                   status.battery_time_to_empty) {
          output += base::StringPrintf(" (%s until shutdown)",
              util::TimeDeltaToString(status.battery_time_to_shutdown).c_str());
        }
      }
      break;
    case PowerSupplyProperties_BatteryState_NOT_PRESENT:
      break;
  }

  return output;
}

PowerStatus::Source::Source(const std::string& id,
                            PowerSupplyProperties::PowerSource::Port port,
                            const std::string& manufacturer_id,
                            const std::string& model_id,
                            double max_power,
                            bool active_by_default)
    : id(id),
      port(port),
      manufacturer_id(manufacturer_id),
      model_id(model_id),
      max_power(max_power),
      active_by_default(active_by_default) {}

PowerStatus::Source::~Source() {}

PowerStatus::PowerStatus()
    : line_power_on(false),
      line_power_voltage(0.0),
      line_power_max_voltage(0.0),
      line_power_current(0.0),
      line_power_max_current(0.0),
      battery_energy(0.0),
      battery_energy_rate(0.0),
      battery_voltage(0.0),
      battery_current(0.0),
      battery_charge(0.0),
      battery_charge_full(0.0),
      battery_charge_full_design(0.0),
      observed_battery_charge_rate(0.0),
      nominal_voltage(0.0),
      is_calculating_battery_time(false),
      battery_percentage(-1.0),
      display_battery_percentage(-1.0),
      battery_is_present(false),
      battery_below_shutdown_threshold(false),
      external_power(PowerSupplyProperties_ExternalPower_DISCONNECTED),
      battery_state(PowerSupplyProperties_BatteryState_NOT_PRESENT),
      supports_dual_role_devices(false) {}

PowerStatus::~PowerStatus() {}

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
const char PowerSupply::kChargeControlLimitMaxFile[] =
    "charge_control_limit_max";
const int PowerSupply::kObservedBatteryChargeRateMinMs = kDefaultPollMs;
const int PowerSupply::kBatteryStabilizedSlackMs = 50;
const double PowerSupply::kLowBatteryShutdownSafetyPercent = 5.0;

PowerSupply::PowerSupply()
    : prefs_(NULL),
      udev_(NULL),
      clock_(new Clock),
      power_status_initialized_(false),
      low_battery_shutdown_percent_(0.0),
      usb_min_ac_watts_(0.0),
      is_suspended_(false),
      full_factor_(1.0) {
}

PowerSupply::~PowerSupply() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kUdevSubsystem, this);
}

void PowerSupply::Init(const base::FilePath& power_supply_path,
                       PrefsInterface* prefs,
                       UdevInterface* udev,
                       bool log_shutdown_thresholds) {
  udev_ = udev;
  udev_->AddSubsystemObserver(kUdevSubsystem, this);

  prefs_ = prefs;
  power_supply_path_ = power_supply_path;

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

  prefs_->GetDouble(kUsbMinAcWattsPref, &usb_min_ac_watts_);

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

  // This log message is needed by the power_LoadTest autotest, but we don't
  // want to spam the logs when this method is called by power_supply_info.
  if (log_shutdown_thresholds) {
    LOG(INFO) << "Using low battery time threshold of "
              << low_battery_shutdown_time_.InSeconds()
              << " secs and using low battery percent threshold of "
              << low_battery_shutdown_percent_;
  }

  std::string ports_string;
  if (prefs_->GetString(kChargingPortsPref, &ports_string)) {
    base::TrimWhitespaceASCII(ports_string, base::TRIM_TRAILING, &ports_string);
    base::StringPairs pairs;
    if (!base::SplitStringIntoKeyValuePairs(ports_string, ' ', '\n', &pairs))
      LOG(FATAL) << "Failed parsing " << kChargingPortsPref << " pref";
    for (const auto& pair : pairs) {
      const PowerSupplyProperties::PowerSource::Port port =
          GetPortFromString(pair.second);
      if (port == PowerSupplyProperties_PowerSource_Port_UNKNOWN) {
        LOG(FATAL) << "Unrecognized port \"" << pair.second << "\" for \""
                   << pair.first << "\" in " << kChargingPortsPref << " pref";
      }
      if (!port_names_.insert(std::make_pair(pair.first, port)).second) {
        LOG(FATAL) << "Duplicate entry for \"" << pair.first << "\" in "
                   << kChargingPortsPref << " pref";
      }
    }
  }

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

bool PowerSupply::SetPowerSource(const std::string& id) {
  // An empty ID means we should write -1 to any power source (we'll use the
  // active one) to ask the kernel to use the battery as the power source.
  // Otherwise, write 0 to the requested power source to activate it.
  const base::FilePath device_path = GetPathForId(
      id.empty() ? power_status_.external_power_source_id : id);
  if (device_path.empty())
    return false;

  const base::FilePath limit_path =
      device_path.Append(kChargeControlLimitMaxFile);
  const std::string value = id.empty() ? "-1" : "0";
  if (!util::WriteFileFully(limit_path, value.c_str(), value.size())) {
    LOG(ERROR) << "Failed to write " << value << " to " << limit_path.value();
    return false;
  }
  return true;
}

void PowerSupply::OnUdevEvent(const std::string& subsystem,
                              const std::string& sysname,
                              UdevAction action) {
  VLOG(1) << "Heard about udev event";
  if (!is_suspended_)
    RefreshImmediately();
}

std::string PowerSupply::GetIdForPath(const base::FilePath& path) const {
  DCHECK(power_supply_path_.IsParent(path))
      << path.value() << " isn't a child of " << power_supply_path_.value();
  return path.BaseName().value();
}

base::FilePath PowerSupply::GetPathForId(const std::string& id) const {
  // Double-check that nobody's playing games with bogus IDs.
  if (id.empty() || id == "." || id == ".." ||
      id.find('/') != std::string::npos) {
    LOG(WARNING) << "Got invalid ID \"" << id << "\"";
    return base::FilePath();
  }
  base::FilePath path = power_supply_path_.Append(id);
  if (!base::DirectoryExists(path)) {
    LOG(WARNING) << "Got invalid ID \"" << id << "\"";
    return base::FilePath();
  }
  return path;
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
  CHECK(prefs_) << "PowerSupply::Init() wasn't called";

  VLOG(1) << "Updating power status";
  PowerStatus status;

  // Track whether we found at least one (possibly offline) power source.
  bool saw_power_source = false;

  // The battery state is dependent on the line power state, so defer reading it
  // until all other directories have been examined.
  base::FilePath battery_path;

  // Iterate through sysfs's power supply information.
  base::FileEnumerator file_enum(
      power_supply_path_, false, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = file_enum.Next(); !path.empty();
       path = file_enum.Next()) {
    if (IsExternalPeripheral(path))
      continue;

    std::string type;
    if (!base::ReadFileToString(path.Append("type"), &type))
      continue;
    base::TrimWhitespaceASCII(type, base::TRIM_TRAILING, &type);

    saw_power_source = true;

    if (type == kBatteryType) {
      if (battery_path.empty())
        battery_path = path;
      else
        LOG(WARNING) << "Multiple batteries; skipping " << path.value();
    } else {
      ReadLinePowerDirectory(path, &status);
    }
  }

  // If no battery was found, assume that the system is actually on AC power.
  if (!status.line_power_on &&
      (battery_path.empty() || !IsBatteryPresent(battery_path))) {
    if (saw_power_source) {
      // Batteryless Chromeboxes sometimes don't report any power sources. If we
      // saw at least one source but it wasn't online, the battery status might
      // be getting misreported, though; log a warning.
      LOG(WARNING) << "Found neither line power nor a battery; assuming that "
                   << "line power is connected";
    }
    status.line_power_on = true;
    status.line_power_type = kMainsType;
    status.external_power = PowerSupplyProperties_ExternalPower_AC;
  }

  // Sort the power source list to make life easier for tests.
  std::sort(status.available_external_power_sources.begin(),
            status.available_external_power_sources.end(),
            SourceComparator());

  // Even though we haven't successfully finished initializing the status yet,
  // save what we have so far so that if we bail out early due to a messed-up
  // battery we'll at least start out knowing whether line power is connected.
  if (!power_status_initialized_)
    power_status_ = status;

  // Finally, read the battery status.
  if (!battery_path.empty() && !ReadBatteryDirectory(battery_path, &status))
    return false;

  // Update running averages and use them to compute battery estimates.
  if (status.battery_is_present) {
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
      charge_samples_->AddSample(status.battery_charge, now);

      if (status.battery_current > 0.0) {
        const double signed_current =
            (status.battery_state ==
             PowerSupplyProperties_BatteryState_DISCHARGING) ?
            -status.battery_current : status.battery_current;
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
  }

  power_status_ = status;
  power_status_initialized_ = true;
  return true;
}

void PowerSupply::ReadLinePowerDirectory(const base::FilePath& path,
                                         PowerStatus* status) {
  // Bidirectional/dual-role ports export a "status" field.
  std::string line_status;
  ReadAndTrimString(path, "status", &line_status);
  const bool is_dual_role_port = !line_status.empty();
  if (is_dual_role_port)
    status->supports_dual_role_devices = true;

  // If "online" is false, nothing is connected.
  int64_t online_value = 0;
  if (!ReadInt64(path, "online", &online_value) || !online_value)
    return;

  // An "Unknown" type indicates a sink-only device that can't supply power.
  std::string type;
  ReadAndTrimString(path, "type", &type);
  if (type == kUnknownType)
    return;

  // Okay, this is a potential power source. Add it to the list.
  const std::string id = GetIdForPath(path);

  // Non-USB PD devices are always active by default. The USB PD kernel driver
  // uses "Mains" for source-only devices (i.e. dedicated chargers) and "USB"
  // for dual-role devices (which aren't active by default). See
  // http://crbug.com/459412 for additional discussion.
  const bool is_usb_type = IsUsbChargerType(type);
  const bool active_by_default = !is_dual_role_port || !is_usb_type;

  const auto port_it = port_names_.find(path.BaseName().value());
  const PowerSupplyProperties::PowerSource::Port port =
      port_it != port_names_.end()
          ? port_it->second
          : PowerSupplyProperties_PowerSource_Port_UNKNOWN;

  std::string manufacturer_id, model_id;
  ReadAndTrimString(path, "manufacturer", &manufacturer_id);
  ReadAndTrimString(path, "model_name", &model_id);

  const double max_voltage = ReadScaledDouble(path, "voltage_max_design");
  const double max_current = ReadScaledDouble(path, "current_max");
  const double max_power = max_voltage * max_current;  // watts

  status->available_external_power_sources.push_back(PowerStatus::Source(
      id, port, manufacturer_id, model_id, max_power, active_by_default));
  VLOG(1) << "Added power source " << id << ": port=" << port
          << " manufacturer=" << manufacturer_id << " model=" << model_id
          << " max_power=" << max_power << " active_by_default="
          << active_by_default;

  // If this is a dual-role device, make sure that we're actually getting
  // charged by it.
  if (is_dual_role_port && line_status != kLinePowerStatusCharging)
    return;

  if (!status->line_power_path.empty()) {
    LOG(WARNING) << "Skipping additional line power source at " << path.value()
                 << " (previously saw " << status->line_power_path << ")";
    return;
  }

  status->line_power_on = true;
  status->line_power_path = path.value();
  status->line_power_type = type;
  status->line_power_voltage = ReadScaledDouble(path, "voltage_now");
  status->line_power_max_voltage = max_voltage;
  status->line_power_current = ReadScaledDouble(path, "current_now");
  status->line_power_max_current = max_current;

  // The USB PD driver reports the maximum power as being 0 watts while it's
  // being determined; avoid reporting a low-power charger in that case.
  const bool max_power_is_less_than_ac_min = max_power > 0.0 &&
      max_power < usb_min_ac_watts_;

  if (!is_dual_role_port && is_usb_type) {
    // On spring, report all non-official chargers (which are reported as type
    // USB rather than Mains) as being low-power.
    status->external_power = PowerSupplyProperties_ExternalPower_USB;
  } else if (is_dual_role_port && max_power_is_less_than_ac_min) {
    // For dual-role USB PD devices, check whether the maximum supported power
    // is below the configured threshold.
    status->external_power = PowerSupplyProperties_ExternalPower_USB;
  } else {
    // Otherwise, report a high-power source.
    status->external_power = PowerSupplyProperties_ExternalPower_AC;
  }
  status->external_power_source_id = id;

  VLOG(1) << "Found line power of type \"" << status->line_power_type
          << "\" at " << path.value();
}

bool PowerSupply::ReadBatteryDirectory(const base::FilePath& path,
                                       PowerStatus* status) {
  VLOG(1) << "Reading battery status from " << path.value();
  status->battery_path = path.value();
  status->battery_is_present = IsBatteryPresent(path);
  if (!status->battery_is_present)
    return true;

  std::string status_value;
  ReadAndTrimString(path, "status", &status_value);

  // POWER_SUPPLY_PROP_VENDOR does not seem to be a valid property
  // defined in <linux/power_supply.h>.
  ReadAndTrimString(path,
      base::PathExists(path.Append("manufacturer")) ? "manufacturer" : "vendor",
      &status->battery_vendor);
  ReadAndTrimString(path, "model_name", &status->battery_model_name);
  ReadAndTrimString(path, "serial_number", &status->battery_serial);
  ReadAndTrimString(path, "technology", &status->battery_technology);

  double voltage = ReadScaledDouble(path, "voltage_now");
  status->battery_voltage = voltage;

  // Attempt to determine nominal voltage for time-remaining calculations. This
  // may or may not be the same as the instantaneous voltage |battery_voltage|,
  // as voltage levels vary over the time the battery is charged or discharged.
  // Some batteries don't have a voltage_min/max_design attribute, so just use
  // the current voltage in that case.
  double nominal_voltage = voltage;
  if (base::PathExists(path.Append("voltage_min_design")))
    nominal_voltage = ReadScaledDouble(path, "voltage_min_design");
  else if (base::PathExists(path.Append("voltage_max_design")))
    nominal_voltage = ReadScaledDouble(path, "voltage_max_design");

  // Nominal voltage is not required to obtain the charge level; if it's
  // missing, just use |battery_voltage|. Save the fact that it was zero so it
  // can be used later to detect cases where battery info has been zeroed out
  // during an in-progress firmware update, though.
  const bool read_zero_nominal_voltage = nominal_voltage == 0.0;
  if (nominal_voltage <= 0) {
    LOG(WARNING) << "Got nominal voltage " << nominal_voltage << "; using "
                 << "instantaneous voltage " << voltage << " instead";
    nominal_voltage = voltage;
  }

  status->nominal_voltage = nominal_voltage;

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
  //
  // Some other batteries have more than that. If it has the charge attributes,
  // just read those and the energy_now attribute.
  double charge_full = 0;
  double charge_full_design = 0;
  double charge = 0;
  double energy = 0;

  if (base::PathExists(path.Append("energy_now")))
    energy = ReadScaledDouble(path, "energy_now");

  if (base::PathExists(path.Append("charge_full"))) {
    charge_full = ReadScaledDouble(path, "charge_full");
    charge_full_design = ReadScaledDouble(path, "charge_full_design");
    charge = ReadScaledDouble(path, "charge_now");
    if (energy <= 0.0)
      energy = charge * nominal_voltage;
  } else if (base::PathExists(path.Append("energy_full"))) {
    // Valid voltage is required to determine the charge so return early if it
    // is not present. In this case, we know nothing about battery state or
    // remaining percentage, so set proper status.
    if (nominal_voltage <= 0) {
      LOG(WARNING) << "Invalid nominal voltage reading for energy-to-charge"
                   << " conversion: " << nominal_voltage;
      return false;
    }
    charge_full = ReadScaledDouble(path, "energy_full") / nominal_voltage;
    charge_full_design = ReadScaledDouble(path, "energy_full_design") /
                         nominal_voltage;
    charge = energy / nominal_voltage;
  } else {
    LOG(WARNING) << "No charge/energy readings for battery";
    return false;
  }

  if (charge == 0.0 && read_zero_nominal_voltage) {
    LOG(WARNING) << "Ignoring reading with zero battery charge and nominal "
                 << "voltage (firmware update in progress?)";
    return false;
  }
  if (charge_full <= 0.0) {
    LOG(WARNING) << "Ignoring reading with battery charge of " << charge
                 << " and battery-full charge of " << charge_full;
    return false;
  }

  status->battery_charge_full = charge_full;
  status->battery_charge_full_design = charge_full_design;
  status->battery_charge = charge;
  status->battery_energy = energy;

  // The current can be reported as negative on some systems but not on others,
  // so it can't be used to determine whether the battery is charging or
  // discharging.
  double current = base::PathExists(path.Append("power_now")) ?
      fabs(ReadScaledDouble(path, "power_now")) / voltage :
      fabs(ReadScaledDouble(path, "current_now"));
  status->battery_current = current;
  status->battery_energy_rate = current * voltage;

  status->battery_percentage = util::ClampPercent(100.0 * charge / charge_full);
  status->display_battery_percentage = util::ClampPercent(
      100.0 * (status->battery_percentage - low_battery_shutdown_percent_) /
      (100.0 * full_factor_ - low_battery_shutdown_percent_));

  const bool is_full = charge >= charge_full * full_factor_;

  if (status->line_power_on) {
    if (is_full) {
      status->battery_state = PowerSupplyProperties_BatteryState_FULL;
    } else if (current > 0.0 &&
               (status_value == kBatteryStatusCharging ||
                status_value == kBatteryStatusFull)) {
      status->battery_state = PowerSupplyProperties_BatteryState_CHARGING;
    } else {
      status->battery_state = PowerSupplyProperties_BatteryState_DISCHARGING;
    }
  } else {
    status->battery_state = PowerSupplyProperties_BatteryState_DISCHARGING;
  }

  return true;
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
