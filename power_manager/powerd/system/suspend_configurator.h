// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_
#define POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/time/time.h>
#include <components/timers/alarm_timer_chromeos.h>

namespace power_manager {

class PrefsInterface;

namespace system {

// Interface to configure suspend-related kernel parameters on startup or
// before suspend as needed.
class SuspendConfiguratorInterface {
 public:
  SuspendConfiguratorInterface() = default;
  SuspendConfiguratorInterface(const SuspendConfiguratorInterface&) = delete;
  SuspendConfiguratorInterface& operator=(const SuspendConfiguratorInterface&) =
      delete;

  virtual ~SuspendConfiguratorInterface() = default;

  // Do pre-suspend configuration and logging just before asking kernel to
  // suspend.
  virtual void PrepareForSuspend(const base::TimeDelta& suspend_duration) = 0;
  // Do post-suspend work just after resuming from suspend. Returns false if the
  // last suspend was a failure. Returns true otherwise.
  virtual bool UndoPrepareForSuspend() = 0;
};

class SuspendConfigurator : public SuspendConfiguratorInterface {
 public:
  // Path to write to enable/disable console during suspend.
  static const base::FilePath kConsoleSuspendPath;

  SuspendConfigurator() = default;
  SuspendConfigurator(const SuspendConfigurator&) = delete;
  SuspendConfigurator& operator=(const SuspendConfigurator&) = delete;

  ~SuspendConfigurator() override = default;

  void Init(PrefsInterface* prefs);

  // SuspendConfiguratorInterface implementation.
  void PrepareForSuspend(const base::TimeDelta& suspend_duration) override;
  bool UndoPrepareForSuspend() override;

  // Sets a prefix path which is used as file system root when testing.
  // Setting to an empty path removes the prefix.
  void set_prefix_path_for_testing(const base::FilePath& file) {
    prefix_path_for_testing_ = file;
  }

 private:
  // Configures whether console should be enabled/disabled during suspend.
  void ConfigureConsoleForSuspend();

  // Returns true if the serial console is enabled.
  bool IsSerialConsoleEnabled();

  // Reads preferences and sets |suspend_mode_|.
  void ReadSuspendMode();

  // Returns new FilePath after prepending |prefix_path_for_testing_| to
  // given file path.
  base::FilePath GetPrefixedFilePath(const base::FilePath& file_path) const;

  PrefsInterface* prefs_ = nullptr;  // weak
  // Prefixing all paths for testing with a temp directory. Empty (no
  // prefix) by default.
  base::FilePath prefix_path_for_testing_;

  // Timer to wake the system from suspend. Set when suspend_duration is passed
  // to  PrepareForSuspend().
  std::unique_ptr<timers::SimpleAlarmTimer> alarm_ =
      timers::SimpleAlarmTimer::Create();

  // Mode for suspend. One of Suspend-to-idle, Power-on-suspend, or
  // Suspend-to-RAM.
  std::string suspend_mode_;
};

}  // namespace system
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_SYSTEM_SUSPEND_CONFIGURATOR_H_
