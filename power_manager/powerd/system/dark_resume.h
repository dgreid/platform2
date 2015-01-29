// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_H_
#define POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_H_

#include <map>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/files/file_path.h>

#include "power_manager/powerd/system/power_supply.h"

namespace power_manager {

class PrefsInterface;

namespace system {

// Returns information related to "dark resume", a mode where the system briefly
// resumes from suspend to check the battery level and possibly shut down
// automatically.
class DarkResumeInterface {
 public:
  enum Action {
    // Suspend the system.
    SUSPEND = 0,
    // Shut the system down immediately.
    SHUT_DOWN,
  };

  DarkResumeInterface() {}
  virtual ~DarkResumeInterface() {}

  // These methods bracket each suspend request. PrepareForSuspendRequest will
  // schedule the dark resume callback if it is able to, and
  // UndoPrepareForSuspendRequest will deschedule it if necessary.
  virtual void PrepareForSuspendRequest() = 0;
  virtual void UndoPrepareForSuspendRequest() = 0;

  // Updates state in anticipation of the system suspending, returning the
  // action that should be performed. If SUSPEND is returned, |suspend_duration|
  // contains the duration for which the system should be suspended or an empty
  // base::TimeDelta() if the caller should not try to set up a wake alarm. This
  // may occur if the system should suspend indefinitely, or if DarkResume was
  // successful in setting a wake alarm for some point in the future.
  // This may be called more than once per suspend request.
  virtual void GetActionForSuspendAttempt(
      Action* action,
      base::TimeDelta* suspend_duration) = 0;

  // Reads the system state to see if it's in a dark resume.
  virtual void HandleSuccessfulResume() = 0;

  // Returns true if the system is currently in dark resume.
  virtual bool InDarkResume() = 0;

  // Returns true if dark resume is enabled on the system.
  virtual bool IsEnabled() = 0;
};

// Real implementation of DarkResumeInterface that interacts with sysfs.
class DarkResume : public DarkResumeInterface {
 public:
  // Within a device directory, kPowerDir contains kActiveFile, kSourceFile, and
  // kWakeupTypeFile.
  static const char kPowerDir[];
  static const char kActiveFile[];
  static const char kSourceFile[];
  static const char kWakeupTypeFile[];

  // Strings to write to sysfs files to enable/disable dark resume functionality
  // at the kernel level.
  static const char kEnabled[];
  static const char kDisabled[];

  // Strings to write to sysfs device wakeup type files to enable dark resume
  // behavior on wakeup by the device.
  static const char kAutomatic[];
  static const char kUnknown[];

  DarkResume();
  virtual ~DarkResume();

  void set_legacy_state_path_for_testing(const base::FilePath& path) {
    legacy_state_path_ = path;
  }

  void set_wakeup_state_path_for_testing(const base::FilePath& path) {
    wakeup_state_path_ = path;
  }

  void set_timer_for_testing(scoped_ptr<base::Timer> timer) {
    timer_ = timer.Pass();
  }

  Action next_action_for_testing() const {
    return next_action_;
  }

  // Reads preferences on how long to suspend, what devices are affected by
  // suspend, and what devices can wake the system up from suspend.
  // Ownership of passed-in pointers remains with the caller.
  void Init(PowerSupplyInterface* power_supply, PrefsInterface* prefs);

  // DarkResumeInterface implementation:
  void PrepareForSuspendRequest() override;
  void UndoPrepareForSuspendRequest() override;
  void GetActionForSuspendAttempt(Action* action,
                                  base::TimeDelta* suspend_duration) override;
  void HandleSuccessfulResume() override;
  bool InDarkResume() override;
  bool IsEnabled() override;

 private:
  // Fills |suspend_durations_|, returning false if the pref was unset or empty.
  bool ReadSuspendDurationsPref();

  // This enables functionality for dark resume in devices that are listed in
  // the prefs_file. The base_file is the name of the sysfs file we are writing
  // to to enable the functionality for dark resume. This includes whether the
  // device should do something different during a dark resume or whether it is
  // a wakeup source for dark resume.
  void GetFiles(std::vector<base::FilePath>* files,
                const std::string& pref_name,
                const std::string& base_file);

  // Writes the passed-in state to all the files in |files|.
  void SetStates(const std::vector<base::FilePath>& files,
                 const std::string& state);

  // Callback which updates the next action and reschedules itself based on the
  // current power status.
  void ScheduleBatteryCheck();

  // Gets the length of time to resuspend for given the current battery level.
  base::TimeDelta GetNextSuspendDuration();

  // Calculates a new |next_action_| to take based on the current battery level,
  // line power status, and shutdown threshold. |next_action_| will then be
  // used on the next suspend to decide whether to shut down or not.
  void UpdateNextAction();

  // Are we currently in dark resume?
  bool in_dark_resume_;

  // Is dark resume enabled?
  bool enabled_;

  // Are we using the new wakeup_type sysfs interface for dark resume?
  bool using_wakeup_type_;

  PowerSupplyInterface* power_supply_;
  PrefsInterface* prefs_;

  // Timer used to schedule system wakeups and check if we need to shut down.
  scoped_ptr<base::Timer> timer_;

  // Two possible dark resume state paths.
  base::FilePath legacy_state_path_;
  base::FilePath wakeup_state_path_;

  // File read to get the dark resume state.
  base::FilePath state_path_;

  // Battery percentage threshold at which the system should shut down after a
  // dark resume.
  double battery_shutdown_threshold_;

  // How long the system should suspend (values) at a given battery percentage
  // (keys).
  typedef std::map<double, base::TimeDelta> SuspendMap;
  SuspendMap suspend_durations_;

  // What the next suspend-time action should be.
  Action next_action_;

  std::vector<base::FilePath> dark_resume_sources_;
  std::vector<base::FilePath> dark_resume_devices_;

  DISALLOW_COPY_AND_ASSIGN(DarkResume);
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_DARK_RESUME_H_
