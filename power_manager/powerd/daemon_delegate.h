// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_DAEMON_DELEGATE_H_
#define POWER_MANAGER_POWERD_DAEMON_DELEGATE_H_

#include <sys/types.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace power_manager {

namespace policy {
class BacklightController;
}  // namespace policy

namespace system {
class AcpiWakeupHelperInterface;
class AmbientLightSensorInterface;
class AmbientLightSensorManagerInterface;
class AudioClientInterface;
class BacklightInterface;
class ChargeControllerHelperInterface;
class DarkResumeInterface;
class DBusWrapperInterface;
class DisplayPowerSetterInterface;
class DisplayWatcherInterface;
class EcHelperInterface;
class InputWatcherInterface;
class LockfileCheckerInterface;
class PeripheralBatteryWatcher;
class PowerSupplyInterface;
class UserProximityWatcherInterface;
class SuspendConfiguratorInterface;
class UdevInterface;
class WakeupSourceIdentifierInterface;
}  // namespace system

class MetricsSenderInterface;
class PrefsInterface;

// Delegate class implementing functionality on behalf of the Daemon class.
// Create*() methods perform any necessary initialization of the returned
// objects.
class DaemonDelegate {
 public:
  DaemonDelegate() {}
  virtual ~DaemonDelegate() {}

  // Crashes if prefs can't be loaded (e.g. due to a missing directory).
  virtual std::unique_ptr<PrefsInterface> CreatePrefs() = 0;

  // Crashes if the connection to the system bus fails.
  virtual std::unique_ptr<system::DBusWrapperInterface> CreateDBusWrapper() = 0;

  // Crashes if udev initialization fails.
  virtual std::unique_ptr<system::UdevInterface> CreateUdev() = 0;

  virtual std::unique_ptr<system::AmbientLightSensorManagerInterface>
  CreateAmbientLightSensorManager(PrefsInterface* prefs) = 0;

  virtual std::unique_ptr<system::DisplayWatcherInterface> CreateDisplayWatcher(
      system::UdevInterface* udev) = 0;

  virtual std::unique_ptr<system::DisplayPowerSetterInterface>
  CreateDisplayPowerSetter(system::DBusWrapperInterface* dbus_wrapper) = 0;

  virtual std::unique_ptr<policy::BacklightController>
  CreateExternalBacklightController(
      system::DisplayWatcherInterface* display_watcher,
      system::DisplayPowerSetterInterface* display_power_setter,
      system::DBusWrapperInterface* dbus_wrapper) = 0;

  // Returns null if the backlight couldn't be initialized.
  virtual std::unique_ptr<system::BacklightInterface> CreateInternalBacklight(
      const base::FilePath& base_path, const std::string& pattern) = 0;

  virtual std::unique_ptr<system::BacklightInterface>
  CreatePluggableInternalBacklight(system::UdevInterface* udev,
                                   const std::string& udev_subsystem,
                                   const base::FilePath& base_path,
                                   const std::string& pattern) = 0;

  virtual std::unique_ptr<policy::BacklightController>
  CreateInternalBacklightController(
      system::BacklightInterface* backlight,
      PrefsInterface* prefs,
      system::AmbientLightSensorInterface* sensor,
      system::DisplayPowerSetterInterface* power_setter,
      system::DBusWrapperInterface* dbus_wrapper,
      LidState initial_lid_state) = 0;

  virtual std::unique_ptr<policy::BacklightController>
  CreateKeyboardBacklightController(
      system::BacklightInterface* backlight,
      PrefsInterface* prefs,
      system::AmbientLightSensorInterface* sensor,
      system::DBusWrapperInterface* dbus_wrapper,
      policy::BacklightController* display_backlight_controller,
      LidState initial_lid_state,
      TabletMode initial_tablet_mode) = 0;

  virtual std::unique_ptr<system::InputWatcherInterface> CreateInputWatcher(
      PrefsInterface* prefs, system::UdevInterface* udev) = 0;

  virtual std::unique_ptr<system::AcpiWakeupHelperInterface>
  CreateAcpiWakeupHelper() = 0;

  virtual std::unique_ptr<system::CrosEcHelperInterface>
  CreateCrosEcHelper() = 0;

  // Test implementations may return null.
  virtual std::unique_ptr<system::PeripheralBatteryWatcher>
  CreatePeripheralBatteryWatcher(system::DBusWrapperInterface* dbus_wrapper,
                                 system::UdevInterface* udev) = 0;

  virtual std::unique_ptr<system::PowerSupplyInterface> CreatePowerSupply(
      const base::FilePath& power_supply_path,
      PrefsInterface* prefs,
      system::UdevInterface* udev,
      system::DBusWrapperInterface* dbus_wrapper,
      BatteryPercentageConverter* battery_percentage_converter) = 0;

  virtual std::unique_ptr<system::UserProximityWatcherInterface>
  CreateUserProximityWatcher(PrefsInterface* prefs,
                             system::UdevInterface* udev) = 0;

  virtual std::unique_ptr<system::DarkResumeInterface> CreateDarkResume(
      PrefsInterface* prefs,
      system::WakeupSourceIdentifierInterface* wakeup_source_identifier) = 0;

  virtual std::unique_ptr<system::AudioClientInterface> CreateAudioClient(
      system::DBusWrapperInterface* dbus_wrapper,
      const base::FilePath& run_dir) = 0;

  virtual std::unique_ptr<system::LockfileCheckerInterface>
  CreateLockfileChecker(const base::FilePath& dir,
                        const std::vector<base::FilePath>& files) = 0;

  virtual std::unique_ptr<MetricsSenderInterface> CreateMetricsSender() = 0;

  virtual std::unique_ptr<system::ChargeControllerHelperInterface>
  CreateChargeControllerHelper() = 0;

  virtual std::unique_ptr<system::SuspendConfiguratorInterface>
  CreateSuspendConfigurator(PrefsInterface* prefs) = 0;

  // Returns the process's PID.
  virtual pid_t GetPid() = 0;

  // Runs |command| asynchronously.
  virtual void Launch(const std::string& command) = 0;

  // Runs |command| synchronously.  The process's exit code is returned.
  virtual int Run(const std::string& command) = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(DaemonDelegate);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_DAEMON_DELEGATE_H_
