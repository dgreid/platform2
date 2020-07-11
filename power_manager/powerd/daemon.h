// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_DAEMON_H_
#define POWER_MANAGER_POWERD_DAEMON_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <dbus/exported_object.h>

#include "power_manager/common/prefs_observer.h"
#include "power_manager/powerd/policy/cellular_controller.h"
#include "power_manager/powerd/policy/charge_controller.h"
#include "power_manager/powerd/policy/input_event_handler.h"
#include "power_manager/powerd/policy/suspender.h"
#include "power_manager/powerd/policy/user_proximity_handler.h"
#include "power_manager/powerd/policy/wifi_controller.h"
#include "power_manager/powerd/system/audio_observer.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/power_supply_observer.h"
#include "power_manager/proto_bindings/suspend.pb.h"

namespace dbus {
class ObjectProxy;
}

namespace power_manager {

class BatteryPercentageConverter;
class DaemonDelegate;
class MetricsSenderInterface;
class PeriodicActivityLogger;
class PrefsInterface;
class StartStopActivityLogger;

namespace metrics {
class MetricsCollector;
}  // namespace metrics

namespace policy {
class BacklightController;
class CellularController;
class InputDeviceController;
class UserProximityHandler;
class ShutdownFromSuspend;
class StateController;
class Suspender;
class ThermalEventHandler;
class WifiController;
}  // namespace policy

namespace system {
class AcpiWakeupHelperInterface;
class AmbientLightSensorManagerInterface;
class ArcTimerManager;
class AudioClientInterface;
class BacklightInterface;
class ChargeControllerHelperInterface;
class DarkResumeInterface;
class DisplayPowerSetterInterface;
class DisplayWatcherInterface;
class CrosEcHelperInterface;
class InputWatcherInterface;
class LockfileCheckerInterface;
class PeripheralBatteryWatcher;
class PowerSupplyInterface;
class UserProximityWatcherInterface;
class SuspendConfiguratorInterface;
class SuspendFreezerInterface;
class ThermalDeviceInterface;
class UdevInterface;
class WakeupSourceIdentifierInterface;
}  // namespace system

class Daemon;

// Main class within the powerd daemon that ties all other classes together.
class Daemon : public policy::InputEventHandler::Delegate,
               public policy::Suspender::Delegate,
               public policy::WifiController::Delegate,
               public policy::CellularController::Delegate,
               public system::AudioObserver,
               public system::DBusWrapperInterface::Observer,
               public system::PowerSupplyObserver {
 public:
  // File used to identify the first instantiation of powerd after a boot.
  // Presence of this file indicates that this is not the first run of powerd
  // after boot.
  static constexpr char kAlreadyRanFileName[] = "already_ran";

  Daemon(DaemonDelegate* delegate, const base::FilePath& run_dir);
  ~Daemon() override;

  void set_wakeup_count_path_for_testing(const base::FilePath& path) {
    wakeup_count_path_ = path;
  }
  void set_oobe_completed_path_for_testing(const base::FilePath& path) {
    oobe_completed_path_ = path;
  }
  void set_suspended_state_path_for_testing(const base::FilePath& path) {
    suspended_state_path_ = path;
  }

  bool first_run_after_boot_for_testing() { return first_run_after_boot_; }

  void Init();

  // If |retry_shutdown_for_lockfile_timer_| is running, triggers it
  // and returns true. Otherwise, returns false.
  bool TriggerRetryShutdownTimerForTesting();

  // Overridden from policy::InputEventHandler::Delegate:
  void HandleLidClosed() override;
  void HandleLidOpened() override;
  void HandlePowerButtonEvent(ButtonState state) override;
  void HandleHoverStateChange(bool hovering) override;
  void HandleTabletModeChange(TabletMode mode) override;
  void ShutDownForPowerButtonWithNoDisplay() override;
  void HandleMissingPowerButtonAcknowledgment() override;
  void ReportPowerButtonAcknowledgmentDelay(base::TimeDelta delay) override;

  // Overridden from policy::Suspender::Delegate:
  int GetInitialSuspendId() override;
  int GetInitialDarkSuspendId() override;
  bool IsLidClosedForSuspend() override;
  bool ReadSuspendWakeupCount(uint64_t* wakeup_count) override;
  void SetSuspendAnnounced(bool announced) override;
  bool GetSuspendAnnounced() override;
  void PrepareToSuspend() override;
  SuspendResult DoSuspend(uint64_t wakeup_count,
                          bool wakeup_count_valid,
                          base::TimeDelta duration) override;
  void UndoPrepareToSuspend(bool success, int num_suspend_attempts) override;
  void GenerateDarkResumeMetrics(
      const std::vector<policy::Suspender::DarkResumeInfo>&
          dark_resume_wake_durations,
      base::TimeDelta suspend_duration) override;
  void ShutDownForFailedSuspend() override;
  void ShutDownFromSuspend() override;

  // Overridden from policy::WifiController::Delegate:
  void SetWifiTransmitPower(RadioTransmitPower power,
                            WifiRegDomain domain) override;

  // Overridden from policy::CellularController::Delegate:
  void SetCellularTransmitPower(RadioTransmitPower power,
                                int64_t dpr_gpio_number) override;

  // Overridden from system::AudioObserver:
  void OnAudioStateChange(bool active) override;

  // Overridden from system::DBusWrapperInterface::Observer:
  void OnDBusNameOwnerChanged(const std::string& name,
                              const std::string& old_owner,
                              const std::string& new_owner) override;

  // Overridden from system::PowerSupplyObserver:
  void OnPowerStatusUpdate() override;

 private:
  class StateControllerDelegate;
  class SuspenderDelegate;

  // Passed to ShutDown() to specify whether the system should power off or
  // reboot.
  enum class ShutdownMode {
    POWER_OFF,
    REBOOT,
  };

  // Convenience method that returns true if |name| exists and is true.
  bool BoolPrefIsTrue(const std::string& name) const;

  // Returns true if a process that wants power management to be blocked is
  // running. |details_out| is updated to contain information about the
  // process(es).
  bool SuspendAndShutdownAreBlocked(std::string* details_out);

  // Runs powerd_setuid_helper. |action| is passed via --action.  If
  // |additional_args| is non-empty, it will be appended to the command. If
  // |wait_for_completion| is true, this function will block until the helper
  // finishes and return the helper's exit code; otherwise it will return 0
  // immediately.
  int RunSetuidHelper(const std::string& action,
                      const std::string& additional_args,
                      bool wait_for_completion);

  // Connects to the D-Bus system bus and exports methods. Does not publish the
  // D-Bus service, as additional methods may need to exported by other classes
  // before that happens.
  void InitDBus();

  // Handles various D-Bus services becoming available or restarting.
  void HandleDisplayServiceAvailableOrRestarted(bool available);
  void HandleSessionManagerAvailableOrRestarted(bool available);

  // Handles other D-Bus services just becoming initially available (i.e.
  // restarts are ignored).
  void HandleCryptohomedAvailable(bool available);

  // Callbacks for handling D-Bus signals and method calls.
  void HandleSessionStateChangedSignal(dbus::Signal* signal);
  void HandleGetTpmStatusResponse(dbus::Response* response);
  std::unique_ptr<dbus::Response> HandleRequestShutdownMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleRequestRestartMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleRequestSuspendMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleVideoActivityMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleUserActivityMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleWakeNotificationMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleSetIsProjectingMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleSetPolicyMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleSetBacklightsForcedOffMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleGetBacklightsForcedOffMethod(
      dbus::MethodCall* method_call);
  // TODO(crbug.com/1036038): Chrome will use "AllowAmbientEQ" feature flag
  // instead of this D-Bus method. To be removed once Chrome finishes cleanup
  // and uprev.
  std::unique_ptr<dbus::Response> HandleHasAmbientColorDeviceMethod(
      dbus::MethodCall* method_call);
  std::unique_ptr<dbus::Response> HandleChangeWifiRegDomainMethod(
      dbus::MethodCall* method_call);

  // Handles information from the session manager about the session state.
  void OnSessionStateChange(const std::string& state_str);

  // Asynchronously asks |cryptohomed_dbus_proxy| (which must be non-null) to
  // return the TPM status, which is handled by HandleGetTpmStatusResponse().
  void RequestTpmStatus();

  // Shuts the system down immediately.
  void ShutDown(ShutdownMode mode, ShutdownReason reason);

  // Starts the suspend process. If |use_external_wakeup_count| is true,
  // passes |external_wakeup_count| to
  // policy::Suspender::RequestSuspendWithExternalWakeupCount();
  void Suspend(SuspendImminent::Reason reason,
               bool use_external_wakeup_count,
               uint64_t external_wakeup_count,
               base::TimeDelta duration);

  // Updates state in |all_backlight_controllers_|.
  void SetBacklightsDimmedForInactivity(bool dimmed);
  void SetBacklightsOffForInactivity(bool off);
  void SetBacklightsSuspended(bool suspended);

  DaemonDelegate* delegate_;  // weak

  std::unique_ptr<PrefsInterface> prefs_;

  std::unique_ptr<system::DBusWrapperInterface> dbus_wrapper_;

  // ObjectProxy objects are owned by |dbus_wrapper_|.
  dbus::ObjectProxy* session_manager_dbus_proxy_ = nullptr;
  // May be null if the TPM status is not needed.
  dbus::ObjectProxy* cryptohomed_dbus_proxy_ = nullptr;

  std::unique_ptr<BatteryPercentageConverter> battery_percentage_converter_;
  std::unique_ptr<StateControllerDelegate> state_controller_delegate_;
  std::unique_ptr<MetricsSenderInterface> metrics_sender_;

  // Many of these members may be null depending on the device's hardware
  // configuration.
  std::unique_ptr<system::AmbientLightSensorManagerInterface>
      light_sensor_manager_;
  std::unique_ptr<system::DisplayWatcherInterface> display_watcher_;
  std::unique_ptr<system::DisplayPowerSetterInterface> display_power_setter_;
  std::unique_ptr<system::BacklightInterface> display_backlight_;
  std::unique_ptr<policy::BacklightController> display_backlight_controller_;
  std::unique_ptr<system::BacklightInterface> keyboard_backlight_;
  std::unique_ptr<policy::BacklightController> keyboard_backlight_controller_;

  std::unique_ptr<system::UdevInterface> udev_;
  std::unique_ptr<system::InputWatcherInterface> input_watcher_;
  std::unique_ptr<policy::StateController> state_controller_;
  std::unique_ptr<policy::InputEventHandler> input_event_handler_;
  std::unique_ptr<system::AcpiWakeupHelperInterface> acpi_wakeup_helper_;
  std::unique_ptr<system::CrosEcHelperInterface> ec_helper_;
  std::unique_ptr<policy::InputDeviceController> input_device_controller_;
  std::unique_ptr<system::AudioClientInterface> audio_client_;  // May be null.
  std::unique_ptr<system::PeripheralBatteryWatcher>
      peripheral_battery_watcher_;  // May be null.
  std::unique_ptr<system::PowerSupplyInterface> power_supply_;
  std::unique_ptr<system::UserProximityWatcherInterface>
      user_proximity_watcher_;
  std::unique_ptr<policy::UserProximityHandler> user_proximity_handler_;
  std::unique_ptr<system::DarkResumeInterface> dark_resume_;
  std::unique_ptr<policy::ShutdownFromSuspend> shutdown_from_suspend_;
  std::unique_ptr<policy::Suspender> suspender_;
  std::unique_ptr<policy::WifiController> wifi_controller_;
  std::unique_ptr<policy::CellularController> cellular_controller_;
  std::unique_ptr<system::SuspendConfiguratorInterface> suspend_configurator_;
  std::unique_ptr<system::SuspendFreezerInterface> suspend_freezer_;
  std::unique_ptr<system::WakeupSourceIdentifierInterface>
      wakeup_source_identifier_;
  std::vector<std::unique_ptr<system::ThermalDeviceInterface>> thermal_devices_;
  std::unique_ptr<policy::ThermalEventHandler> thermal_event_handler_;

  std::unique_ptr<metrics::MetricsCollector> metrics_collector_;

  std::unique_ptr<system::ChargeControllerHelperInterface>
      charge_controller_helper_;
  std::unique_ptr<policy::ChargeController> charge_controller_;

  // Object that manages all operations related to timers in the ARC instance.
  std::unique_ptr<system::ArcTimerManager> arc_timer_manager_;

  // Checks if a lockfile exists indicating that power management should be
  // overridden (typically due to a firmware update).
  std::unique_ptr<system::LockfileCheckerInterface>
      power_override_lockfile_checker_;

  // Weak pointers to |display_backlight_controller_| and
  // |keyboard_backlight_controller_|, if non-null.
  std::vector<policy::BacklightController*> all_backlight_controllers_;

  // True if the kFactoryModePref pref indicates that the system is running in
  // the factory, implying that much of powerd's functionality should be
  // disabled.
  bool factory_mode_ = false;

  // True once the shutdown process has started. Remains true until the
  // system has powered off.
  bool shutting_down_ = false;

  // Recurring timer that's started if a shutdown request is deferred due to
  // |power_override_lockfile_checker_| reporting lockfiles. ShutDown() is
  // called repeatedly so the system will eventually be shut down after the
  // lockfile(s) are gone.
  base::RepeatingTimer retry_shutdown_for_lockfile_timer_;

  // Timer that periodically calls RequestTpmStatus() if
  // |cryptohome_dbus_proxy_| is non-null.
  base::RepeatingTimer tpm_status_timer_;

  // Delay with which |tpm_status_timer_| should fire.
  base::TimeDelta tpm_status_interval_;

  // File containing the number of wakeup events.
  base::FilePath wakeup_count_path_;

  // File that's created once the out-of-box experience has been completed.
  base::FilePath oobe_completed_path_;

  // Directory under /run that holds run-time data related to powerd.
  base::FilePath run_dir_;

  // Path to file that's touched before the system suspends and unlinked after
  // it resumes. Used by crash-reporter to avoid reporting unclean shutdowns
  // that occur while the system is suspended (i.e. probably due to the battery
  // charge reaching zero).
  base::FilePath suspended_state_path_;

  // Path to a file that's touched when a suspend attempt's commencement is
  // announced to other processes and unlinked when the attempt's completion is
  // announced. Used to detect cases where powerd was restarted
  // mid-suspend-attempt and didn't announce that the attempt finished.
  base::FilePath suspend_announced_path_;

  // Path to file under /run that's inspected and then touched at startup.
  // If the file doesn't already exist, then |first_run_after_boot_| is
  // set to true.
  base::FilePath already_ran_path_;

  // Last session state that we have been informed of. Initialized as stopped.
  SessionState session_state_ = SessionState::STOPPED;

  // Set to true if powerd touched a file for crash-reporter before
  // suspending. If true, the file will be unlinked after resuming.
  bool created_suspended_state_file_ = false;

  // True if the "mosys" command should be used to record suspend and resume
  // timestamps in eventlog.
  bool log_suspend_with_mosys_eventlog_ = false;

  // True if the system should suspend to idle.
  bool suspend_to_idle_ = false;

  // True if this is the first instantiation of powerd after boot.
  bool first_run_after_boot_ = false;

  // Used to log video, user, and audio activity and hovering.
  std::unique_ptr<PeriodicActivityLogger> video_activity_logger_;
  std::unique_ptr<PeriodicActivityLogger> user_activity_logger_;
  std::unique_ptr<StartStopActivityLogger> audio_activity_logger_;
  std::unique_ptr<StartStopActivityLogger> hovering_logger_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<Daemon> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_DAEMON_H_
