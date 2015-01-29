// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/dark_resume.h"

#include <stdint.h>

#include <algorithm>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <components/timers/alarm_timer.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"

namespace power_manager {
namespace system {

namespace {

// Default file describing whether the system is currently in dark resume.
const char kDarkResumeStatePath[] = "/sys/power/dark_resume_state";

// In kernel 3.14 and later, we switch over to wakeup_type instead of
// dark_resume_state.
const char kWakeupTypePath[] = "/sys/power/wakeup_type";

}  // namespace

const char DarkResume::kPowerDir[] = "power";
const char DarkResume::kActiveFile[] = "dark_resume_active";
const char DarkResume::kSourceFile[] = "dark_resume_source";
const char DarkResume::kEnabled[] = "enabled";
const char DarkResume::kDisabled[] = "disabled";
// For kernel 3.14 and later
const char DarkResume::kWakeupTypeFile[] = "wakeup_type";
const char DarkResume::kAutomatic[] = "automatic";
const char DarkResume::kUnknown[] = "unknown";

DarkResume::DarkResume()
    : in_dark_resume_(false),
      using_wakeup_type_(false),
      power_supply_(NULL),
      prefs_(NULL),
      legacy_state_path_(kDarkResumeStatePath),
      wakeup_state_path_(kWakeupTypePath),
      battery_shutdown_threshold_(0.0),
      next_action_(SUSPEND) {
}

DarkResume::~DarkResume() {
  SetStates(dark_resume_sources_, using_wakeup_type_ ? kUnknown : kDisabled);
  SetStates(dark_resume_devices_, kDisabled);
}

void DarkResume::Init(PowerSupplyInterface* power_supply,
                      PrefsInterface* prefs) {
  power_supply_ = power_supply;
  prefs_ = prefs;

  scoped_ptr<timers::AlarmTimer> timer(
      new timers::AlarmTimer(true /* retain_user_task */,
                             false /* is_repeating */));
  if (timer->can_wake_from_suspend())
    timer_ = timer.Pass();

  bool disable = false;
  enabled_ = (!prefs_->GetBool(kDisableDarkResumePref, &disable) || !disable) &&
      ReadSuspendDurationsPref();
  LOG(INFO) << "Dark resume user space " << (enabled_ ? "enabled" : "disabled");

  std::string source_file;
  std::string source_state;
  state_path_ = wakeup_state_path_;
  if (base::PathExists(state_path_)) {
    using_wakeup_type_ = true;
    source_file = kWakeupTypeFile;
    source_state = enabled_ ? kAutomatic : kUnknown;
  } else if (base::PathExists(legacy_state_path_)) {
    state_path_ = legacy_state_path_;
    using_wakeup_type_ = false;
    source_file = kSourceFile;
    source_state = enabled_ ? kEnabled : kDisabled;
  } else {
    enabled_ = false;
    LOG(WARNING) << "Dark resume state path not found";
  }
  GetFiles(&dark_resume_sources_, kDarkResumeSourcesPref, source_file);
  GetFiles(&dark_resume_devices_, kDarkResumeDevicesPref, kActiveFile);
  SetStates(dark_resume_sources_, source_state);
  SetStates(dark_resume_devices_, (enabled_ ? kEnabled : kDisabled));
}

void DarkResume::PrepareForSuspendRequest() {
  if (timer_ && enabled_)
    ScheduleBatteryCheck();
}

void DarkResume::UndoPrepareForSuspendRequest() {
  if (timer_)
    timer_->Stop();

  in_dark_resume_ = false;
}

void DarkResume::GetActionForSuspendAttempt(Action* action,
                                            base::TimeDelta* suspend_duration) {
  DCHECK(action);
  DCHECK(suspend_duration);

  if (!enabled_ || !power_supply_->RefreshImmediately()) {
    *action = SUSPEND;
    *suspend_duration = base::TimeDelta();
    return;
  }

  if (timer_) {
    *suspend_duration = base::TimeDelta();
  } else {
    UpdateNextAction();
    *suspend_duration = GetNextSuspendDuration();
  }

  *action = next_action_;
}

void DarkResume::ScheduleBatteryCheck() {
  if (!power_supply_->RefreshImmediately())
    return;

  UpdateNextAction();

  timer_->Start(FROM_HERE,
                GetNextSuspendDuration(),
                base::Bind(&DarkResume::ScheduleBatteryCheck,
                           base::Unretained(this)));
}

base::TimeDelta DarkResume::GetNextSuspendDuration() {
  const double battery = power_supply_->GetPowerStatus().battery_percentage;
  SuspendMap::iterator suspend_it = suspend_durations_.upper_bound(battery);
  if (suspend_it != suspend_durations_.begin())
    suspend_it--;
  return suspend_it->second;
}

void DarkResume::UpdateNextAction() {
  const PowerStatus status = power_supply_->GetPowerStatus();
  const double battery = status.battery_percentage;
  const bool line_power = status.line_power_on;
  const bool in_dark_resume = InDarkResume();
  LOG(INFO) << (in_dark_resume ? "In" : "Not in") << " dark resume with "
            << "battery at " << battery << "% and line power "
            << (line_power ? "on" : "off");

  // If suspending from the non-dark-resume state, or if the battery level has
  // actually increased since the previous suspend attempt, update the shutdown
  // threshold.
  if (!in_dark_resume || battery > battery_shutdown_threshold_) {
    battery_shutdown_threshold_ = battery;
    LOG(INFO) << "Updated shutdown threshold to " << battery << "%";
  }

  next_action_ = (battery < battery_shutdown_threshold_ && !line_power)
      ? SHUT_DOWN : SUSPEND;
}

void DarkResume::HandleSuccessfulResume() {
  if (!enabled_) {
    in_dark_resume_ = false;
    return;
  }

  std::string buf;
  if (!base::ReadFileToString(state_path_, &buf)) {
    PLOG(ERROR) << "Unable to read " << state_path_.value();
    in_dark_resume_ = false;
    return;
  }

  base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
  if (using_wakeup_type_) {
    in_dark_resume_ = buf == kAutomatic;
  } else {
    uint64_t value = 0;
    in_dark_resume_ = base::StringToUint64(buf, &value) && value;
  }
}

bool DarkResume::InDarkResume() {
  return in_dark_resume_;
}

bool DarkResume::IsEnabled() {
  return enabled_;
}

bool DarkResume::ReadSuspendDurationsPref() {
  suspend_durations_.clear();

  std::string data;
  if (!prefs_->GetString(kDarkResumeSuspendDurationsPref, &data))
    return false;

  base::StringPairs pairs;
  base::TrimWhitespaceASCII(data, base::TRIM_TRAILING, &data);
  if (!base::SplitStringIntoKeyValuePairs(data, ' ', '\n', &pairs)) {
    LOG(ERROR) << "Unable to parse " << kDarkResumeSuspendDurationsPref;
    return false;
  }
  for (size_t i = 0; i < pairs.size(); ++i) {
    double battery_level = 0.0;
    int suspend_duration = 0;
    if (!base::StringToDouble(pairs[i].first, &battery_level) ||
        !base::StringToInt(pairs[i].second,  &suspend_duration)) {
      LOG(ERROR) << "Unable to parse values on line " << i << " of "
                 << kDarkResumeSuspendDurationsPref;
      return false;
    }

    if (suspend_duration % base::TimeDelta::FromDays(1).InSeconds() == 0) {
      LOG(ERROR) << "Suspend duration in " << kDarkResumeSuspendDurationsPref
                 << " cannot be multiple of 86400";
      return false;
    }

    suspend_durations_[battery_level] =
        base::TimeDelta::FromSeconds(suspend_duration);
  }
  return !suspend_durations_.empty();
}

void DarkResume::GetFiles(std::vector<base::FilePath>* files,
                          const std::string& pref_name,
                          const std::string& base_file) {
  files->clear();

  std::string data;
  if (!prefs_->GetString(pref_name, &data))
    return;

  std::vector<std::string> lines;
  base::SplitString(data, '\n', &lines);
  for (size_t i = 0; i < lines.size(); ++i) {
    base::FilePath path = base::FilePath(lines[i]);
    path = path.AppendASCII(kPowerDir);
    path = path.AppendASCII(base_file.c_str());
    files->push_back(path);
  }
}

void DarkResume::SetStates(const std::vector<base::FilePath>& files,
                           const std::string& state) {
  for (std::vector<base::FilePath>::const_iterator iter = files.begin();
       iter != files.end(); ++iter) {
    if (!util::WriteFileFully(*iter, state.c_str(), state.length())) {
      PLOG(ERROR) << "Failed writing \"" << state << "\" to " << iter->value();
    }
  }
}

}  // namespace system
}  // namespace power_manager
