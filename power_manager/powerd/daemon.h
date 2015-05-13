// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_DAEMON_H_
#define POWER_MANAGER_POWERD_DAEMON_H_

#include <stdint.h>

#include <string>
#include <vector>

#include <base/compiler_specific.h>
#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/memory/scoped_ptr.h>
#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "power_manager/common/prefs_observer.h"
#include "power_manager/powerd/policy/backlight_controller_observer.h"
#include "power_manager/powerd/policy/input_controller.h"
#include "power_manager/powerd/policy/suspender.h"
#include "power_manager/powerd/system/audio_observer.h"
#include "power_manager/powerd/system/power_supply_observer.h"

class MetricsSender;

namespace power_manager {

class DBusSender;
class MetricsCollector;
class MetricsSender;
class Prefs;

namespace policy {
class BacklightController;
class KeyboardBacklightController;
class StateController;
class Suspender;
class WakeupController;
}  // namespace policy

namespace system {
class AcpiWakeupHelper;
class AmbientLightSensor;
class AudioClient;
class DarkResume;
class DisplayPowerSetter;
class DisplayWatcher;
class InputWatcher;
class InternalBacklight;
class PeripheralBatteryWatcher;
class PowerSupply;
class Udev;
}  // namespace system

class Daemon;

// Pointer to a member function for handling D-Bus method calls. If an empty
// scoped_ptr is returned, an empty (but successful) response will be sent.
typedef scoped_ptr<dbus::Response> (Daemon::*DBusMethodCallMemberFunction)(
    dbus::MethodCall*);

// Main class within the powerd daemon that ties all other classes together.
class Daemon : public policy::BacklightControllerObserver,
               public policy::InputController::Delegate,
               public policy::Suspender::Delegate,
               public system::AudioObserver,
               public system::PowerSupplyObserver {
 public:
  Daemon(const base::FilePath& read_write_prefs_dir,
         const base::FilePath& read_only_prefs_dir,
         const base::FilePath& run_dir);
  virtual ~Daemon();

  void Init();

  // Overridden from policy::BacklightControllerObserver:
  void OnBrightnessChanged(
      double brightness_percent,
      policy::BacklightController::BrightnessChangeCause cause,
      policy::BacklightController* source) override;

  // Overridden from policy::InputController::Delegate:
  void HandleLidClosed() override;
  void HandleLidOpened() override;
  void HandlePowerButtonEvent(ButtonState state) override;
  void HandleHoverStateChanged(bool hovering) override;
  void DeferInactivityTimeoutForVT2() override;
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
  void UndoPrepareToSuspend(bool success,
                            int num_suspend_attempts,
                            bool canceled_while_in_dark_resume) override;
  void GenerateDarkResumeMetrics(
      const std::vector<policy::Suspender::DarkResumeInfo>&
          dark_resume_wake_durations,
      base::TimeDelta suspend_duration) override;

  void ShutDownForFailedSuspend() override;
  void ShutDownForDarkResume() override;
  bool CanSafelyExitDarkResume() override;

  // Overridden from system::AudioObserver:
  void OnAudioStateChange(bool active) override;

  // Overridden from system::PowerSupplyObserver:
  void OnPowerStatusUpdate() override;

 private:
  class StateControllerDelegate;
  class SuspenderDelegate;

  // Passed to ShutDown() to specify whether the system should power off or
  // reboot.
  enum ShutdownMode {
    SHUTDOWN_MODE_POWER_OFF,
    SHUTDOWN_MODE_REBOOT,
  };

  // Convenience method that returns true if |name| exists and is true.
  bool BoolPrefIsTrue(const std::string& name) const;

  // Decreases/increases the keyboard brightness; direction should be +1 for
  // increase and -1 for decrease.
  void AdjustKeyboardBrightness(int direction);

  // Emits a D-Bus signal named |signal_name| announcing that backlight
  // brightness was changed to |brightness_percent| due to |cause|.
  void SendBrightnessChangedSignal(
      double brightness_percent,
      policy::BacklightController::BrightnessChangeCause cause,
      const std::string& signal_name);

  // Connects to the D-Bus system bus and exports methods.
  void InitDBus();

  // Handles various D-Bus services becoming available or restarting.
  void HandleChromeAvailableOrRestarted(bool available);
  void HandleSessionManagerAvailableOrRestarted(bool available);
  void HandleCrasAvailableOrRestarted(bool available);

  // Handles other D-Bus services just becoming initially available (i.e.
  // restarts are ignored).
  void HandleUpdateEngineAvailable(bool available);
  void HandleCryptohomedAvailable(bool available);

  // Handles changes to D-Bus name ownership.
  void HandleDBusNameOwnerChanged(dbus::Signal* signal);

  // Handles the result of an attempt to connect to a D-Bus signal. Logs an
  // error on failure.
  void HandleDBusSignalConnected(const std::string& interface,
                                 const std::string& signal,
                                 bool success);

  // Exports |method_name| and uses |member| to handle calls.
  void ExportDBusMethod(const std::string& method_name,
                        DBusMethodCallMemberFunction member);

  // Callbacks for handling D-Bus signals and method calls.
  void HandleSessionStateChangedSignal(dbus::Signal* signal);
  void HandleUpdateEngineStatusUpdateSignal(dbus::Signal* signal);
  void HandleCrasNodesChangedSignal(dbus::Signal* signal);
  void HandleCrasActiveOutputNodeChangedSignal(dbus::Signal* signal);
  void HandleCrasNumberOfActiveStreamsChanged(dbus::Signal* signal);
  void HandleGetTpmStatusResponse(dbus::Response* response);
  scoped_ptr<dbus::Response> HandleRequestShutdownMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleRequestRestartMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleRequestSuspendMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleDecreaseScreenBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleIncreaseScreenBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleGetScreenBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleSetScreenBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleDecreaseKeyboardBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleIncreaseKeyboardBrightnessMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleGetPowerSupplyPropertiesMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleVideoActivityMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleUserActivityMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleSetIsProjectingMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleSetPolicyMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandleSetPowerSourceMethod(
      dbus::MethodCall* method_call);
  scoped_ptr<dbus::Response> HandlePowerButtonAcknowledgment(
      dbus::MethodCall* method_call);

  // Handles information from the session manager about the session state.
  void OnSessionStateChange(const std::string& state_str);

  // Handles the "operation" field from an update engine status message.
  void OnUpdateOperation(const std::string& operation);

  // Asynchronously asks |cryptohomed_dbus_proxy| (which must be non-null) to
  // return the TPM status, which is handled by HandleGetTpmStatusResponse().
  void RequestTpmStatus();

  // Shuts the system down immediately.
  void ShutDown(ShutdownMode mode, ShutdownReason reason);

  // Starts the suspend process. If |use_external_wakeup_count| is true,
  // passes |external_wakeup_count| to
  // policy::Suspender::RequestSuspendWithExternalWakeupCount();
  void Suspend(bool use_external_wakeup_count, uint64_t external_wakeup_count);

  // Updates state in |backlight_controller_| and |keyboard_controller_|
  // (if non-NULL).
  void SetBacklightsDimmedForInactivity(bool dimmed);
  void SetBacklightsOffForInactivity(bool off);
  void SetBacklightsSuspended(bool suspended);
  void SetBacklightsDocked(bool docked);

  scoped_ptr<Prefs> prefs_;

  scoped_refptr<dbus::Bus> bus_;
  dbus::ExportedObject* powerd_dbus_object_;  // weak; owned by |bus_|
  dbus::ObjectProxy* chrome_dbus_proxy_;  // weak; owned by |bus_|
  dbus::ObjectProxy* session_manager_dbus_proxy_;  // weak; owned by |bus_|
  dbus::ObjectProxy* cras_dbus_proxy_;  // weak; owned by |bus_| and may be NULL
  dbus::ObjectProxy* update_engine_dbus_proxy_;  // weak; owned by |bus_|
  // May be null if the TPM status is not needed.
  dbus::ObjectProxy* cryptohomed_dbus_proxy_;  // weak; owned by |bus_|

  scoped_ptr<StateControllerDelegate> state_controller_delegate_;
  scoped_ptr<MetricsSender> metrics_sender_;
  scoped_ptr<DBusSender> dbus_sender_;

  scoped_ptr<system::AmbientLightSensor> light_sensor_;
  scoped_ptr<system::DisplayWatcher> display_watcher_;
  scoped_ptr<system::DisplayPowerSetter> display_power_setter_;
  scoped_ptr<system::InternalBacklight> display_backlight_;
  scoped_ptr<policy::BacklightController> display_backlight_controller_;
  scoped_ptr<system::InternalBacklight> keyboard_backlight_;
  scoped_ptr<policy::KeyboardBacklightController>
      keyboard_backlight_controller_;

  scoped_ptr<system::Udev> udev_;
  scoped_ptr<system::InputWatcher> input_watcher_;
  scoped_ptr<policy::StateController> state_controller_;
  scoped_ptr<policy::InputController> input_controller_;
  scoped_ptr<system::AcpiWakeupHelper> acpi_wakeup_helper_;
  scoped_ptr<policy::WakeupController> wakeup_controller_;
  scoped_ptr<system::AudioClient> audio_client_;  // May be NULL.
  scoped_ptr<system::PeripheralBatteryWatcher> peripheral_battery_watcher_;
  scoped_ptr<system::PowerSupply> power_supply_;
  scoped_ptr<system::DarkResume> dark_resume_;
  scoped_ptr<policy::Suspender> suspender_;

  scoped_ptr<MetricsCollector> metrics_collector_;

  // True once the shutdown process has started. Remains true until the
  // system has powered off.
  bool shutting_down_;

  // Recurring timer that's started if a shutdown request is deferred due to a
  // firmware update. ShutDown() is called repeatedly so the system will
  // eventually be shut down after the firmware-updating process exits.
  base::Timer retry_shutdown_for_firmware_update_timer_;

  // Timer that periodically calls RequestTpmStatus() if
  // |cryptohome_dbus_proxy_| is non-null.
  base::RepeatingTimer<Daemon> tpm_status_timer_;

  // Delay with which |tpm_status_timer_| should fire.
  base::TimeDelta tpm_status_interval_;

  // Path to a file that's touched when a suspend attempt's commencement is
  // announced to other processes and unlinked when the attempt's completion is
  // announced. Used to detect cases where powerd was restarted
  // mid-suspend-attempt and didn't announce that the attempt finished.
  base::FilePath suspend_announced_path_;

  // Last session state that we have been informed of. Initialized as stopped.
  SessionState session_state_;

  // Set to true if powerd touched a file for crash-reporter before
  // suspending. If true, the file will be unlinked after resuming.
  bool created_suspended_state_file_;

  // True if VT switching should be disabled before the system is suspended.
  bool lock_vt_before_suspend_;

  // True if the "mosys" command should be used to record suspend and resume
  // timestamps in eventlog.
  bool log_suspend_with_mosys_eventlog_;

  // True if the system can properly transition from dark resume to fully
  // resumed.
  bool can_safely_exit_dark_resume_;

  // Must come last so that weak pointers will be invalidated before other
  // members are destroyed.
  base::WeakPtrFactory<Daemon> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_DAEMON_H_
