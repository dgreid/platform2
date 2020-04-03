// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/daemon.h"

#include <inttypes.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>

#include "cryptohome/proto_bindings/rpc.pb.h"
#include "power_manager/common/activity_logger.h"
#include "power_manager/common/battery_percentage_converter.h"
#include "power_manager/common/metrics_sender.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#if USE_BUFFET
#include "power_manager/powerd/buffet/command_handlers.h"
#endif
#include "power_manager/powerd/daemon_delegate.h"
#include "power_manager/powerd/metrics_collector.h"
#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/input_device_controller.h"
#include "power_manager/powerd/policy/keyboard_backlight_controller.h"
#include "power_manager/powerd/policy/shutdown_from_suspend.h"
#include "power_manager/powerd/policy/state_controller.h"
#include "power_manager/powerd/system/acpi_wakeup_helper_interface.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager.h"
#include "power_manager/powerd/system/arc_timer_manager.h"
#include "power_manager/powerd/system/audio_client_interface.h"
#include "power_manager/powerd/system/backlight_interface.h"
#include "power_manager/powerd/system/charge_controller_helper_interface.h"
#include "power_manager/powerd/system/cros_ec_helper_interface.h"
#include "power_manager/powerd/system/dark_resume_interface.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/powerd/system/display/display_power_setter.h"
#include "power_manager/powerd/system/display/display_watcher.h"
#include "power_manager/powerd/system/event_device_interface.h"
#include "power_manager/powerd/system/input_watcher_interface.h"
#include "power_manager/powerd/system/lockfile_checker.h"
#include "power_manager/powerd/system/peripheral_battery_watcher.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/smart_discharge_configurator.h"
#include "power_manager/powerd/system/suspend_configurator.h"
#include "power_manager/powerd/system/suspend_freezer.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/user_proximity_watcher_interface.h"
#include "power_manager/powerd/system/wake_on_dp_configurator.h"
#include "power_manager/powerd/system/wakeup_source_identifier.h"
#include "power_manager/powerd/system/wilco_charge_controller_helper.h"
#include "power_manager/proto_bindings/idle.pb.h"
#include "power_manager/proto_bindings/policy.pb.h"

#if USE_BUFFET
namespace dbus {
class Bus;
}  // namespace dbus
#endif  // USE_BUFFET

namespace power_manager {
namespace {

// Default values for |*_path_| members (which can be overridden for tests).
const char kDefaultSuspendedStatePath[] =
    "/var/lib/power_manager/powerd_suspended";
const char kDefaultWakeupCountPath[] = "/sys/power/wakeup_count";
const char kDefaultOobeCompletedPath[] = "/home/chronos/.oobe_completed";

// Directory checked for lockfiles indicating that powerd shouldn't suspend or
// shut down the system (typically due to a firmware update).
const char kPowerOverrideLockfileDir[] = "/run/lock/power_override";

// Basename appended to |run_dir| (see Daemon's c'tor) to produce
// |suspend_announced_path_|.
const char kSuspendAnnouncedFile[] = "suspend_announced";

// Strings for states that powerd cares about from the session manager's
// SessionStateChanged signal. This value is defined in the session_manager
// codebase.
const char kSessionStarted[] = "started";

// After noticing that power management is overridden while suspending, wait up
// to this long for the lockfile(s) to be removed before reporting a suspend
// failure. The event loop is blocked during this period.
constexpr base::TimeDelta kSuspendLockfileTimeout =
    base::TimeDelta::FromMilliseconds(500);

// Interval between successive polls during kSuspendLockfileTimeout.
constexpr base::TimeDelta kSuspendLockfilePollInterval =
    base::TimeDelta::FromMilliseconds(100);

// Interval between attempts to retry shutting the system down while power
// management is overridden, in seconds.
constexpr base::TimeDelta kShutdownLockfileRetryInterval =
    base::TimeDelta::FromSeconds(5);

// Maximum amount of time to wait for responses to D-Bus method calls to other
// processes.
const int kSessionManagerDBusTimeoutMs = 3000;
const int kCryptohomedDBusTimeoutMs = 2 * 60 * 1000;  // Two minutes.

// Interval between log messages while user, audio, or video activity is
// ongoing, in seconds.
const int kLogOngoingActivitySec = 180;

// Time after the last report from Chrome of video or user activity at which
// point a message is logged about the activity having stopped. Chrome reports
// these every five seconds, but longer delays here reduce the amount of log
// spam due to sporadic activity.
const int kLogVideoActivityStoppedDelaySec = 20;
const int kLogUserActivityStoppedDelaySec = 20;

// Delay to wait before logging that hovering has stopped. This is ideally
// smaller than kKeyboardBacklightKeepOnMsPref; otherwise the backlight can be
// turned off before the hover-off event that triggered it is logged.
const int64_t kLogHoveringStoppedDelaySec = 20;

// Passes |method_call| to |handler| and passes the response to
// |response_sender|. If |handler| returns NULL, an empty response is
// created and sent.
void HandleSynchronousDBusMethodCall(
    base::Callback<std::unique_ptr<dbus::Response>(dbus::MethodCall*)> handler,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  std::unique_ptr<dbus::Response> response = handler.Run(method_call);
  if (!response)
    response = dbus::Response::FromMethodCall(method_call);
  std::move(response_sender).Run(std::move(response));
}

// Creates a new "invalid args" reply to |method_call|.
std::unique_ptr<dbus::Response> CreateInvalidArgsError(
    dbus::MethodCall* method_call, std::string message) {
  return std::unique_ptr<dbus::Response>(dbus::ErrorResponse::FromMethodCall(
      method_call, DBUS_ERROR_INVALID_ARGS, message));
}

}  // namespace

// static
constexpr char Daemon::kAlreadyRanFileName[];

// Performs actions requested by |state_controller_|.  The reason that
// this is a nested class of Daemon rather than just being implemented as
// part of Daemon is to avoid method naming conflicts.
class Daemon::StateControllerDelegate
    : public policy::StateController::Delegate {
 public:
  explicit StateControllerDelegate(Daemon* daemon) : daemon_(daemon) {}
  ~StateControllerDelegate() override { daemon_ = nullptr; }

  // Overridden from policy::StateController::Delegate:
  bool IsUsbInputDeviceConnected() override {
    return daemon_->input_watcher_->IsUSBInputDeviceConnected();
  }

  bool IsOobeCompleted() override {
    return base::PathExists(base::FilePath(daemon_->oobe_completed_path_));
  }

  bool IsHdmiAudioActive() override {
    return daemon_->audio_client_ ? daemon_->audio_client_->GetHdmiActive()
                                  : false;
  }

  bool IsHeadphoneJackPlugged() override {
    return daemon_->audio_client_
               ? daemon_->audio_client_->GetHeadphoneJackPlugged()
               : false;
  }

  void DimScreen() override { daemon_->SetBacklightsDimmedForInactivity(true); }

  void UndimScreen() override {
    daemon_->SetBacklightsDimmedForInactivity(false);
  }

  void TurnScreenOff() override {
    daemon_->SetBacklightsOffForInactivity(true);
  }

  void TurnScreenOn() override {
    daemon_->SetBacklightsOffForInactivity(false);
  }

  void LockScreen() override {
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerLockScreen);
    daemon_->dbus_wrapper_->CallMethodSync(
        daemon_->session_manager_dbus_proxy_, &method_call,
        base::TimeDelta::FromMilliseconds(kSessionManagerDBusTimeoutMs));
  }

  void Suspend(policy::StateController::ActionReason reason) override {
    SuspendImminent::Reason suspend_reason = SuspendImminent_Reason_OTHER;
    switch (reason) {
      case policy::StateController::ActionReason::IDLE:
        suspend_reason = SuspendImminent_Reason_IDLE;
        break;
      case policy::StateController::ActionReason::LID_CLOSED:
        suspend_reason = SuspendImminent_Reason_LID_CLOSED;
        break;
    }
    daemon_->Suspend(suspend_reason, false, 0, base::TimeDelta());
  }

  void StopSession() override {
    // This session manager method takes a string argument, although it
    // doesn't currently do anything with it.
    dbus::MethodCall method_call(login_manager::kSessionManagerInterface,
                                 login_manager::kSessionManagerStopSession);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString("");
    daemon_->dbus_wrapper_->CallMethodSync(
        daemon_->session_manager_dbus_proxy_, &method_call,
        base::TimeDelta::FromMilliseconds(kSessionManagerDBusTimeoutMs));
  }

  void ShutDown() override {
    daemon_->ShutDown(ShutdownMode::POWER_OFF,
                      ShutdownReason::STATE_TRANSITION);
  }

  void ReportUserActivityMetrics() override {
    daemon_->metrics_collector_->GenerateUserActivityMetrics();
  }

 private:
  Daemon* daemon_;  // weak

  DISALLOW_COPY_AND_ASSIGN(StateControllerDelegate);
};

Daemon::Daemon(DaemonDelegate* delegate, const base::FilePath& run_dir)
    : delegate_(delegate),
      state_controller_delegate_(new StateControllerDelegate(this)),
      state_controller_(new policy::StateController),
      input_event_handler_(new policy::InputEventHandler),
      input_device_controller_(new policy::InputDeviceController),
      shutdown_from_suspend_(std::make_unique<policy::ShutdownFromSuspend>()),
      suspender_(new policy::Suspender),
      wifi_controller_(std::make_unique<policy::WifiController>()),
      cellular_controller_(std::make_unique<policy::CellularController>()),
      metrics_collector_(new metrics::MetricsCollector),
      arc_timer_manager_(std::make_unique<system::ArcTimerManager>()),
      retry_shutdown_for_lockfile_timer_(),
      wakeup_count_path_(kDefaultWakeupCountPath),
      oobe_completed_path_(kDefaultOobeCompletedPath),
      run_dir_(run_dir),
      suspended_state_path_(kDefaultSuspendedStatePath),
      suspend_announced_path_(run_dir.Append(kSuspendAnnouncedFile)),
      already_ran_path_(run_dir.Append(Daemon::kAlreadyRanFileName)),
      video_activity_logger_(new PeriodicActivityLogger(
          "Video activity",
          base::TimeDelta::FromSeconds(kLogVideoActivityStoppedDelaySec),
          base::TimeDelta::FromSeconds(kLogOngoingActivitySec))),
      user_activity_logger_(new PeriodicActivityLogger(
          "User activity",
          base::TimeDelta::FromSeconds(kLogUserActivityStoppedDelaySec),
          base::TimeDelta::FromSeconds(kLogOngoingActivitySec))),
      audio_activity_logger_(new StartStopActivityLogger(
          "Audio activity",
          base::TimeDelta(),
          base::TimeDelta::FromSeconds(kLogOngoingActivitySec))),
      hovering_logger_(new StartStopActivityLogger(
          "Hovering",
          base::TimeDelta::FromSeconds(kLogHoveringStoppedDelaySec),
          base::TimeDelta())),
      weak_ptr_factory_(this) {}

Daemon::~Daemon() {
  if (dbus_wrapper_)
    dbus_wrapper_->RemoveObserver(this);
  if (audio_client_)
    audio_client_->RemoveObserver(this);
  if (power_supply_)
    power_supply_->RemoveObserver(this);
}

void Daemon::Init() {
  // Check if this is the first run of powerd after boot.
  first_run_after_boot_ = !base::PathExists(already_ran_path_);
  if (first_run_after_boot_) {
    if (base::WriteFile(already_ran_path_, "", 0) != 0)
      PLOG(ERROR) << "Couldn't create " << already_ran_path_.value();
  }

  prefs_ = delegate_->CreatePrefs();
  InitDBus();

  factory_mode_ = BoolPrefIsTrue(kFactoryModePref);
  if (factory_mode_)
    LOG(INFO) << "Factory mode enabled; most functionality will be disabled";

  metrics_sender_ = delegate_->CreateMetricsSender();
  udev_ = delegate_->CreateUdev();
  input_watcher_ = delegate_->CreateInputWatcher(prefs_.get(), udev_.get());
  suspend_configurator_ = delegate_->CreateSuspendConfigurator(prefs_.get());
  suspend_freezer_ = delegate_->CreateSuspendFreezer(prefs_.get());
  wakeup_source_identifier_ =
      std::make_unique<system::WakeupSourceIdentifier>(udev_.get());

  const TabletMode tablet_mode = input_watcher_->GetTabletMode();
  if (tablet_mode == TabletMode::ON)
    LOG(INFO) << "Tablet mode enabled at startup";
  const LidState lid_state = input_watcher_->QueryLidState();
  if (lid_state == LidState::CLOSED)
    LOG(INFO) << "Lid closed at startup";

  display_watcher_ = delegate_->CreateDisplayWatcher(udev_.get());
  display_power_setter_ =
      delegate_->CreateDisplayPowerSetter(dbus_wrapper_.get());

  // Ignore the ALS and backlights in factory mode.
  if (!factory_mode_) {
    light_sensor_manager_ =
        delegate_->CreateAmbientLightSensorManager(prefs_.get());

    if (BoolPrefIsTrue(kExternalDisplayOnlyPref)) {
      display_backlight_controller_ =
          delegate_->CreateExternalBacklightController(
              display_watcher_.get(), display_power_setter_.get(),
              dbus_wrapper_.get());
    } else {
      display_backlight_ = delegate_->CreateInternalBacklight(
          base::FilePath(kInternalBacklightPath), kInternalBacklightPattern);
      if (!display_backlight_) {
        LOG(ERROR) << "Failed to initialize display backlight under "
                   << kInternalBacklightPath << " using pattern "
                   << kInternalBacklightPattern;
      } else {
        display_backlight_controller_ =
            delegate_->CreateInternalBacklightController(
                display_backlight_.get(), prefs_.get(),
                light_sensor_manager_->GetSensorForInternalBacklight(),
                display_power_setter_.get(), dbus_wrapper_.get(), lid_state);
      }
    }
    if (display_backlight_controller_)
      all_backlight_controllers_.push_back(display_backlight_controller_.get());

    if (BoolPrefIsTrue(kHasKeyboardBacklightPref)) {
      keyboard_backlight_ = delegate_->CreatePluggableInternalBacklight(
          udev_.get(), kKeyboardBacklightUdevSubsystem,
          base::FilePath(kKeyboardBacklightPath), kKeyboardBacklightPattern);
      keyboard_backlight_controller_ =
          delegate_->CreateKeyboardBacklightController(
              keyboard_backlight_.get(), prefs_.get(),
              light_sensor_manager_->GetSensorForKeyboardBacklight(),
              dbus_wrapper_.get(), display_backlight_controller_.get(),
              lid_state, tablet_mode);
      all_backlight_controllers_.push_back(
          keyboard_backlight_controller_.get());
    }
  }

  prefs_->GetBool(kMosysEventlogPref, &log_suspend_with_mosys_eventlog_);
  prefs_->GetBool(kSuspendToIdlePref, &suspend_to_idle_);

  battery_percentage_converter_ =
      BatteryPercentageConverter::CreateFromPrefs(prefs_.get());

  power_supply_ = delegate_->CreatePowerSupply(
      base::FilePath(kPowerStatusPath), prefs_.get(), udev_.get(),
      dbus_wrapper_.get(), battery_percentage_converter_.get());
  power_supply_->AddObserver(this);
  if (!power_supply_->RefreshImmediately())
    LOG(ERROR) << "Initial power supply refresh failed; brace for weirdness";
  const system::PowerStatus power_status = power_supply_->GetPowerStatus();

  metrics_collector_->Init(prefs_.get(), display_backlight_controller_.get(),
                           keyboard_backlight_controller_.get(), power_status,
                           first_run_after_boot_);

  dark_resume_ = delegate_->CreateDarkResume(prefs_.get(),
                                             wakeup_source_identifier_.get());

  shutdown_from_suspend_->Init(prefs_.get(), power_supply_.get());

  suspender_->Init(this, dbus_wrapper_.get(), dark_resume_.get(),
                   display_watcher_.get(), wakeup_source_identifier_.get(),
                   shutdown_from_suspend_.get(), prefs_.get());

  input_event_handler_->Init(input_watcher_.get(), this, display_watcher_.get(),
                             dbus_wrapper_.get(), prefs_.get());

  acpi_wakeup_helper_ = delegate_->CreateAcpiWakeupHelper();
  ec_helper_ = delegate_->CreateCrosEcHelper();
  input_device_controller_->Init(display_backlight_controller_.get(),
                                 udev_.get(), acpi_wakeup_helper_.get(),
                                 ec_helper_.get(), lid_state, tablet_mode,
                                 DisplayMode::NORMAL, prefs_.get());

  const PowerSource power_source =
      power_status.line_power_on ? PowerSource::AC : PowerSource::BATTERY;
  state_controller_->Init(state_controller_delegate_.get(), prefs_.get(),
                          dbus_wrapper_.get(), power_source, lid_state);

  if (BoolPrefIsTrue(kUseCrasPref)) {
    audio_client_ = delegate_->CreateAudioClient(dbus_wrapper_.get(), run_dir_);
    audio_client_->AddObserver(this);
  }

  wifi_controller_->Init(this, prefs_.get(), udev_.get(), tablet_mode);
  cellular_controller_->Init(this, prefs_.get());

  peripheral_battery_watcher_ = delegate_->CreatePeripheralBatteryWatcher(
      dbus_wrapper_.get(), udev_.get());
  power_override_lockfile_checker_ = delegate_->CreateLockfileChecker(
      base::FilePath(kPowerOverrideLockfileDir), {});

  user_proximity_watcher_ =
      delegate_->CreateUserProximityWatcher(prefs_.get(), udev_.get());
  user_proximity_handler_ = std::make_unique<policy::UserProximityHandler>();
  user_proximity_handler_->Init(user_proximity_watcher_.get(),
                                wifi_controller_.get(),
                                cellular_controller_.get());

  arc_timer_manager_->Init(dbus_wrapper_.get());

  if (BoolPrefIsTrue(kHasChargeControllerPref)) {
    charge_controller_helper_ = delegate_->CreateChargeControllerHelper();
    charge_controller_ = std::make_unique<policy::ChargeController>(),
    charge_controller_->Init(charge_controller_helper_.get(),
                             battery_percentage_converter_.get());
  }

  // Asynchronously undo the previous force-lid-open request to the EC (if there
  // was one).
  if (!factory_mode_ && BoolPrefIsTrue(kUseLidPref))
    RunSetuidHelper("set_force_lid_open", "--noforce_lid_open", false);

  // This needs to happen *after* all D-Bus methods are exported:
  // https://crbug.com/331431
  CHECK(dbus_wrapper_->PublishService()) << "Failed to publish D-Bus service";

  // configure wake on dp only if the preference is set.
  bool wake_on_dp = false;
  if (prefs_->GetBool(kWakeOnDpPref, &wake_on_dp))
    system::ConfigureWakeOnDp(wake_on_dp);

  // Configure wake for the EC.
  if (acpi_wakeup_helper_->IsSupported()) {
    acpi_wakeup_helper_->SetWakeupEnabled("CREC", true);
  }

  // Configure Smart Discharge in EC.
  int64_t to_zero_hr = -1, cutoff_ua = -1, hibernate_ua = -1;
  prefs_->GetInt64(kSmartDischargeToZeroHrPref, &to_zero_hr);
  prefs_->GetInt64(kCutoffPowerUaPref, &cutoff_ua);
  prefs_->GetInt64(kHibernatePowerUaPref, &hibernate_ua);
  system::ConfigureSmartDischarge(to_zero_hr, cutoff_ua, hibernate_ua);

  // Call this last to ensure that all of our members are already initialized.
  OnPowerStatusUpdate();
}

bool Daemon::TriggerRetryShutdownTimerForTesting() {
  if (!retry_shutdown_for_lockfile_timer_.IsRunning())
    return false;

  retry_shutdown_for_lockfile_timer_.user_task().Run();
  return true;
}

bool Daemon::BoolPrefIsTrue(const std::string& name) const {
  bool value = false;
  return prefs_->GetBool(name, &value) && value;
}

bool Daemon::SuspendAndShutdownAreBlocked(std::string* details_out) {
  const std::vector<base::FilePath> paths =
      power_override_lockfile_checker_->GetValidLockfiles();
  *details_out = util::JoinPaths(paths, ", ");
  return !paths.empty();
}

int Daemon::RunSetuidHelper(const std::string& action,
                            const std::string& additional_args,
                            bool wait_for_completion) {
  std::string command = kSetuidHelperPath + std::string(" --action=" + action);
  if (!additional_args.empty())
    command += " " + additional_args;
  if (wait_for_completion) {
    return delegate_->Run(command.c_str());
  } else {
    delegate_->Launch(command.c_str());
    return 0;
  }
}

void Daemon::HandleLidClosed() {
  LOG(INFO) << "Lid closed";
  // It is important that we notify InputDeviceController first so that it can
  // inhibit input devices quickly. StateController will issue a blocking call
  // to Chrome which can take longer than a second.
  input_device_controller_->SetLidState(LidState::CLOSED);
  state_controller_->HandleLidStateChange(LidState::CLOSED);
  for (auto controller : all_backlight_controllers_)
    controller->HandleLidStateChange(LidState::CLOSED);

  dbus_wrapper_->EmitBareSignal(kLidClosedSignal);
}

void Daemon::HandleLidOpened() {
  LOG(INFO) << "Lid opened";
  suspender_->HandleLidOpened();
  state_controller_->HandleLidStateChange(LidState::OPEN);
  input_device_controller_->SetLidState(LidState::OPEN);
  for (auto controller : all_backlight_controllers_)
    controller->HandleLidStateChange(LidState::OPEN);

  dbus_wrapper_->EmitBareSignal(kLidOpenedSignal);
}

void Daemon::HandlePowerButtonEvent(ButtonState state) {
  // Don't log spammy repeat events if we see them.
  if (state != ButtonState::REPEAT)
    LOG(INFO) << "Power button " << ButtonStateToString(state);
  metrics_collector_->HandlePowerButtonEvent(state);
  if (state == ButtonState::DOWN) {
    delegate_->Launch("sync");
    for (auto controller : all_backlight_controllers_)
      controller->HandlePowerButtonPress();
  }
}

void Daemon::HandleHoverStateChange(bool hovering) {
  if (hovering)
    hovering_logger_->OnActivityStarted();
  else
    hovering_logger_->OnActivityStopped();

  for (auto controller : all_backlight_controllers_)
    controller->HandleHoverStateChange(hovering);
}

void Daemon::HandleTabletModeChange(TabletMode mode) {
  DCHECK_NE(mode, TabletMode::UNSUPPORTED);
  LOG(INFO) << "Tablet mode " << TabletModeToString(mode);
  state_controller_->HandleTabletModeChange(mode);
  input_device_controller_->SetTabletMode(mode);
  for (auto controller : all_backlight_controllers_)
    controller->HandleTabletModeChange(mode);
  wifi_controller_->HandleTabletModeChange(mode);
  cellular_controller_->HandleTabletModeChange(mode);
}

void Daemon::ShutDownForPowerButtonWithNoDisplay() {
  LOG(INFO) << "Shutting down due to power button press while no display is "
            << "connected";
  metrics_collector_->HandlePowerButtonEvent(ButtonState::DOWN);
  ShutDown(ShutdownMode::POWER_OFF, ShutdownReason::USER_REQUEST);
}

void Daemon::HandleMissingPowerButtonAcknowledgment() {
  LOG(INFO) << "Didn't receive power button acknowledgment from Chrome";
}

void Daemon::ReportPowerButtonAcknowledgmentDelay(base::TimeDelta delay) {
  metrics_collector_->SendPowerButtonAcknowledgmentDelayMetric(delay);
}

int Daemon::GetInitialSuspendId() {
  // Take powerd's PID modulo 2**15 (/proc/sys/kernel/pid_max is currently
  // 2**15, but just in case...) and multiply it by 2**16, leaving it able to
  // fit in a signed 32-bit int. This allows for 2**16 suspend attempts and
  // suspend delays per powerd run before wrapping or intruding on another
  // run's ID range (neither of which should be particularly problematic, but
  // doing this reduces the chances of a confused client that's using stale
  // IDs from a previous powerd run being able to conflict with the new run's
  // IDs).
  return (delegate_->GetPid() % 32768) * 65536 + 1;
}

int Daemon::GetInitialDarkSuspendId() {
  // We use the upper half of the suspend ID space for dark suspend attempts.
  // Assuming that we will go through dark suspend IDs faster than the regular
  // suspend IDs, we should never have a collision between the suspend ID and
  // the dark suspend ID until the dark suspend IDs wrap around.
  return GetInitialSuspendId() + 32768;
}

bool Daemon::IsLidClosedForSuspend() {
  return input_watcher_->QueryLidState() == LidState::CLOSED;
}

bool Daemon::ReadSuspendWakeupCount(uint64_t* wakeup_count) {
  DCHECK(wakeup_count);
  std::string buf;
  LOG(INFO) << "Reading wakeup count from " << wakeup_count_path_.value();
  if (base::ReadFileToString(wakeup_count_path_, &buf)) {
    base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
    if (base::StringToUint64(buf, wakeup_count)) {
      LOG(INFO) << "Read wakeup count " << *wakeup_count;
      return true;
    }
    LOG(ERROR) << "Could not parse wakeup count from \"" << buf << "\"";
  } else {
    PLOG(ERROR) << "Could not read " << wakeup_count_path_.value();
  }
  return false;
}

void Daemon::SetSuspendAnnounced(bool announced) {
  if (announced) {
    if (base::WriteFile(suspend_announced_path_, nullptr, 0) < 0)
      PLOG(ERROR) << "Couldn't create " << suspend_announced_path_.value();
  } else {
    if (!base::DeleteFile(suspend_announced_path_, false))
      PLOG(ERROR) << "Couldn't delete " << suspend_announced_path_.value();
  }
}

bool Daemon::GetSuspendAnnounced() {
  return base::PathExists(suspend_announced_path_);
}

void Daemon::PrepareToSuspend() {
  // Before announcing the suspend request, notify the backlight controller so
  // it can turn the backlight off and tell the kernel to resume the current
  // level after resuming.  This must occur before Chrome is told that the
  // system is going to suspend (Chrome turns the display back on while leaving
  // the backlight off).
  SetBacklightsSuspended(true);

  power_supply_->SetSuspended(true);
  if (audio_client_)
    audio_client_->SetSuspended(true);
  metrics_collector_->PrepareForSuspend();
}

policy::Suspender::Delegate::SuspendResult Daemon::DoSuspend(
    uint64_t wakeup_count, bool wakeup_count_valid, base::TimeDelta duration) {
  // If power management is overridden by a lockfile, spin for a bit to wait for
  // the process to finish: http://crosbug.com/p/38947
  base::TimeDelta elapsed;
  std::string details;
  while (SuspendAndShutdownAreBlocked(&details)) {
    if (elapsed >= kSuspendLockfileTimeout) {
      LOG(INFO) << "Aborting suspend attempt for lockfile(s): " << details;
      return policy::Suspender::Delegate::SuspendResult::FAILURE;
    }
    elapsed += kSuspendLockfilePollInterval;
    base::PlatformThread::Sleep(kSuspendLockfilePollInterval);
  }

  // Touch a file that crash-reporter can inspect later to determine
  // whether the system was suspended while an unclean shutdown occurred.
  // If the file already exists, assume that crash-reporter hasn't seen it
  // yet and avoid unlinking it after resume.
  created_suspended_state_file_ = false;
  if (!base::PathExists(suspended_state_path_)) {
    if (base::WriteFile(suspended_state_path_, nullptr, 0) == 0)
      created_suspended_state_file_ = true;
    else
      PLOG(ERROR) << "Unable to create " << suspended_state_path_.value();
  }

  // This command is run synchronously to ensure that it finishes before the
  // system is suspended.
  if (log_suspend_with_mosys_eventlog_) {
    RunSetuidHelper("mosys_eventlog", "--mosys_eventlog_code=0xa7", true);
  }

  std::vector<std::string> args;
  if (wakeup_count_valid) {
    args.push_back("--suspend_wakeup_count_valid");
    args.push_back(
        base::StringPrintf("--suspend_wakeup_count=%" PRIu64, wakeup_count));
  }

  if (suspend_to_idle_)
    args.push_back("--suspend_to_idle");

  suspend_configurator_->PrepareForSuspend(duration);

  system::FreezeResult freeze_result =
      suspend_freezer_->FreezeUserspace(wakeup_count, wakeup_count_valid);
  if (freeze_result == system::FreezeResult::FAILURE) {
    LOG(ERROR) << "Failed to freeze userspace processes. Aborting suspend";
    return policy::Suspender::Delegate::SuspendResult::FAILURE;
  } else if (freeze_result == system::FreezeResult::CANCELED) {
    return policy::Suspender::Delegate::SuspendResult::CANCELED;
  }

  const int exit_code =
      RunSetuidHelper("suspend", base::JoinString(args, " "), true);
  LOG(INFO) << "powerd_suspend returned " << exit_code;

  if (log_suspend_with_mosys_eventlog_)
    RunSetuidHelper("mosys_eventlog", "--mosys_eventlog_code=0xa8", false);

  if (created_suspended_state_file_) {
    if (!base::DeleteFile(base::FilePath(suspended_state_path_), false))
      PLOG(ERROR) << "Failed to delete " << suspended_state_path_.value();
  }

  if (!suspend_freezer_->ThawUserspace()) {
    LOG(ERROR) << "Failed to thaw userspace processes";
    suspend_configurator_->UndoPrepareForSuspend();
    return policy::Suspender::Delegate::SuspendResult::FAILURE;
  }

  if (!suspend_configurator_->UndoPrepareForSuspend())
    return policy::Suspender::Delegate::SuspendResult::FAILURE;

  // These exit codes are defined in powerd/powerd_suspend.
  switch (exit_code) {
    case 0:
      return policy::Suspender::Delegate::SuspendResult::SUCCESS;
    case 1:
      return policy::Suspender::Delegate::SuspendResult::FAILURE;
    case 2:  // Wakeup event received before write to wakeup_count.
    case 3:  // Wakeup event received after write to wakeup_count.
      return policy::Suspender::Delegate::SuspendResult::CANCELED;
    default:
      LOG(ERROR) << "Treating unexpected exit code " << exit_code
                 << " as suspend failure";
      return policy::Suspender::Delegate::SuspendResult::FAILURE;
  }
}

void Daemon::UndoPrepareToSuspend(bool success, int num_suspend_attempts) {
  LidState lid_state = input_watcher_->QueryLidState();

  // Update the lid state first so that resume does not turn the internal
  // backlight on if the lid is still closed on resume.
  for (auto controller : all_backlight_controllers_)
    controller->HandleLidStateChange(lid_state);

  // Let State controller know about resume with the latest lid state.
  state_controller_->HandleResume(lid_state);

  // Resume the backlight right after announcing resume. This might be where we
  // turn on the display, so we want to do this as early as possible. This
  // happens when we idle suspend (and the requested power state in Chrome is
  // off for the displays).
  SetBacklightsSuspended(false);

  if (audio_client_)
    audio_client_->SetSuspended(false);
  power_supply_->SetSuspended(false);

  if (success)
    metrics_collector_->HandleResume(num_suspend_attempts);
  else if (num_suspend_attempts > 0)
    metrics_collector_->HandleCanceledSuspendRequest(num_suspend_attempts);
}

void Daemon::GenerateDarkResumeMetrics(
    const std::vector<policy::Suspender::DarkResumeInfo>&
        dark_resume_wake_durations,
    base::TimeDelta suspend_duration) {
  metrics_collector_->GenerateDarkResumeMetrics(dark_resume_wake_durations,
                                                suspend_duration);
}

void Daemon::ShutDownForFailedSuspend() {
  ShutDown(ShutdownMode::POWER_OFF, ShutdownReason::SUSPEND_FAILED);
}

void Daemon::ShutDownFromSuspend() {
  ShutDown(ShutdownMode::POWER_OFF, ShutdownReason::SHUTDOWN_FROM_SUSPEND);
}

void Daemon::SetWifiTransmitPower(RadioTransmitPower power,
                                  WifiRegDomain domain) {
  const std::string power_arg = (power == RadioTransmitPower::LOW)
                                    ? "--wifi_transmit_power_tablet"
                                    : "--nowifi_transmit_power_tablet";
  std::string domain_arg = "--wifi_transmit_power_domain=none";
  switch (domain) {
    case WifiRegDomain::FCC:
      domain_arg = "--wifi_transmit_power_domain=fcc";
      break;
    case WifiRegDomain::EU:
      domain_arg = "--wifi_transmit_power_domain=eu";
      break;
    case WifiRegDomain::REST_OF_WORLD:
      domain_arg = "--wifi_transmit_power_domain=rest-of-world";
      break;
    default:
      break;
  }
  const std::string args =
      base::StringPrintf("%s %s", power_arg.c_str(), domain_arg.c_str());
  LOG(INFO) << ((power == RadioTransmitPower::LOW) ? "Enabling" : "Disabling")
            << " tablet mode wifi transmit power";
  RunSetuidHelper("set_wifi_transmit_power", args, false);
}

void Daemon::SetCellularTransmitPower(RadioTransmitPower power,
                                      int64_t dpr_gpio_number) {
  const bool is_power_low = (power == RadioTransmitPower::LOW);
  const std::string args = base::StringPrintf(
      "--cellular_transmit_power_low=%s "
      "--cellular_transmit_power_gpio=%" PRId64,
      is_power_low ? "true" : "false", dpr_gpio_number);
  LOG(INFO) << "Setting cellular transmit power "
            << (is_power_low ? "low" : "high");
  RunSetuidHelper("set_cellular_transmit_power", args, false);
}

void Daemon::OnAudioStateChange(bool active) {
  if (active)
    audio_activity_logger_->OnActivityStarted();
  else
    audio_activity_logger_->OnActivityStopped();
  state_controller_->HandleAudioStateChange(active);
}

void Daemon::OnDBusNameOwnerChanged(const std::string& name,
                                    const std::string& old_owner,
                                    const std::string& new_owner) {
  if (name == login_manager::kSessionManagerServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << name << " ownership changed to " << new_owner;
    HandleSessionManagerAvailableOrRestarted(true);
  } else if (name == chromeos::kDisplayServiceName && !new_owner.empty()) {
    LOG(INFO) << "D-Bus " << name << " ownership changed to " << new_owner;
    HandleDisplayServiceAvailableOrRestarted(true);
  }
  suspender_->HandleDBusNameOwnerChanged(name, old_owner, new_owner);
}

void Daemon::OnPowerStatusUpdate() {
  const system::PowerStatus status = power_supply_->GetPowerStatus();
  if (status.battery_is_present)
    LOG(INFO) << system::GetPowerStatusBatteryDebugString(status);

  metrics_collector_->HandlePowerStatusUpdate(status);

  const PowerSource power_source =
      status.line_power_on ? PowerSource::AC : PowerSource::BATTERY;
  for (auto controller : all_backlight_controllers_)
    controller->HandlePowerSourceChange(power_source);
  state_controller_->HandlePowerSourceChange(power_source);

  if (status.battery_is_present && status.battery_below_shutdown_threshold) {
    if (factory_mode_) {
      LOG(INFO) << "Battery is low, but not shutting down in factory mode";
    } else {
      LOG(INFO) << "Shutting down due to low battery ("
                << base::StringPrintf("%0.2f", status.battery_percentage)
                << "%, "
                << util::TimeDeltaToString(status.battery_time_to_empty)
                << " until empty, "
                << base::StringPrintf("%0.3f",
                                      status.observed_battery_charge_rate)
                << "A observed charge rate)";
      ShutDown(ShutdownMode::POWER_OFF, ShutdownReason::LOW_BATTERY);
    }
  }
}

void Daemon::InitDBus() {
  dbus_wrapper_ = delegate_->CreateDBusWrapper();
  dbus_wrapper_->AddObserver(this);

  dbus::ObjectProxy* display_service_proxy = dbus_wrapper_->GetObjectProxy(
      chromeos::kDisplayServiceName, chromeos::kDisplayServicePath);
  dbus_wrapper_->RegisterForServiceAvailability(
      display_service_proxy,
      base::Bind(&Daemon::HandleDisplayServiceAvailableOrRestarted,
                 weak_ptr_factory_.GetWeakPtr()));

  session_manager_dbus_proxy_ =
      dbus_wrapper_->GetObjectProxy(login_manager::kSessionManagerServiceName,
                                    login_manager::kSessionManagerServicePath);
  dbus_wrapper_->RegisterForServiceAvailability(
      session_manager_dbus_proxy_,
      base::Bind(&Daemon::HandleSessionManagerAvailableOrRestarted,
                 weak_ptr_factory_.GetWeakPtr()));
  dbus_wrapper_->RegisterForSignal(
      session_manager_dbus_proxy_, login_manager::kSessionManagerInterface,
      login_manager::kSessionStateChangedSignal,
      base::Bind(&Daemon::HandleSessionStateChangedSignal,
                 weak_ptr_factory_.GetWeakPtr()));

  int64_t tpm_threshold = 0;
  prefs_->GetInt64(kTpmCounterSuspendThresholdPref, &tpm_threshold);
  if (tpm_threshold > 0) {
    cryptohomed_dbus_proxy_ = dbus_wrapper_->GetObjectProxy(
        cryptohome::kCryptohomeServiceName, cryptohome::kCryptohomeServicePath);
    dbus_wrapper_->RegisterForServiceAvailability(
        cryptohomed_dbus_proxy_, base::Bind(&Daemon::HandleCryptohomedAvailable,
                                            weak_ptr_factory_.GetWeakPtr()));

    int64_t tpm_status_sec = 0;
    prefs_->GetInt64(kTpmStatusIntervalSecPref, &tpm_status_sec);
    tpm_status_interval_ = base::TimeDelta::FromSeconds(tpm_status_sec);
  }

  // Export Daemon's D-Bus method calls.
  typedef std::unique_ptr<dbus::Response> (Daemon::*DaemonMethod)(
      dbus::MethodCall*);
  const std::map<const char*, DaemonMethod> kDaemonMethods = {
      {kRequestShutdownMethod, &Daemon::HandleRequestShutdownMethod},
      {kRequestRestartMethod, &Daemon::HandleRequestRestartMethod},
      {kRequestSuspendMethod, &Daemon::HandleRequestSuspendMethod},
      {kHandleVideoActivityMethod, &Daemon::HandleVideoActivityMethod},
      {kHandleUserActivityMethod, &Daemon::HandleUserActivityMethod},
      {kHandleWakeNotificationMethod, &Daemon::HandleWakeNotificationMethod},
      {kSetIsProjectingMethod, &Daemon::HandleSetIsProjectingMethod},
      {kSetPolicyMethod, &Daemon::HandleSetPolicyMethod},
      {kSetBacklightsForcedOffMethod,
       &Daemon::HandleSetBacklightsForcedOffMethod},
      {kGetBacklightsForcedOffMethod,
       &Daemon::HandleGetBacklightsForcedOffMethod},
      {kHasAmbientColorDeviceMethod,
       &Daemon::HandleHasAmbientColorDeviceMethod},
      {kChangeWifiRegDomainMethod, &Daemon::HandleChangeWifiRegDomainMethod},
  };
  for (const auto& it : kDaemonMethods) {
    dbus_wrapper_->ExportMethod(
        it.first, base::Bind(&HandleSynchronousDBusMethodCall,
                             base::Bind(it.second, base::Unretained(this))));
  }

#if USE_BUFFET
  // There's no underlying dbus::Bus object when we're being tested.
  dbus::Bus* bus = dbus_wrapper_->GetBus();
  if (bus) {
    buffet::InitCommandHandlers(
        bus, base::Bind(&Daemon::ShutDown, weak_ptr_factory_.GetWeakPtr(),
                        ShutdownMode::REBOOT, ShutdownReason::USER_REQUEST));
  }
#endif  // USE_BUFFET
}

void Daemon::HandleDisplayServiceAvailableOrRestarted(bool available) {
  if (!available) {
    LOG(ERROR) << "Failed waiting for DisplayService to become available";
    return;
  }
  for (auto controller : all_backlight_controllers_)
    controller->HandleDisplayServiceStart();

  // When running in the factory, we avoid initializing any backlight
  // controllers, but we need to still tell Chrome to initially turn displays on
  // so it will restore the correct display power state when returning from VT2:
  // http://b/78436034
  if (factory_mode_) {
    DCHECK(all_backlight_controllers_.empty());
    display_power_setter_->SetDisplayPower(chromeos::DISPLAY_POWER_ALL_ON,
                                           base::TimeDelta());
  }
}

void Daemon::HandleSessionManagerAvailableOrRestarted(bool available) {
  if (!available) {
    LOG(ERROR) << "Failed waiting for session manager to become available";
    return;
  }

  dbus::MethodCall method_call(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerRetrieveSessionState);
  std::unique_ptr<dbus::Response> response = dbus_wrapper_->CallMethodSync(
      session_manager_dbus_proxy_, &method_call,
      base::TimeDelta::FromMilliseconds(kSessionManagerDBusTimeoutMs));
  if (!response)
    return;

  std::string state;
  dbus::MessageReader reader(response.get());
  if (!reader.PopString(&state)) {
    LOG(ERROR) << "Unable to read "
               << login_manager::kSessionManagerRetrieveSessionState << " args";
    return;
  }
  OnSessionStateChange(state);
}

void Daemon::HandleCryptohomedAvailable(bool available) {
  if (!available) {
    LOG(ERROR) << "Failed waiting for cryptohomed to become available";
    return;
  }
  if (!cryptohomed_dbus_proxy_)
    return;

  RequestTpmStatus();
  if (tpm_status_interval_ > base::TimeDelta::FromSeconds(0)) {
    tpm_status_timer_.Start(FROM_HERE, tpm_status_interval_, this,
                            &Daemon::RequestTpmStatus);
  }
}

void Daemon::HandleSessionStateChangedSignal(dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  std::string state;
  if (reader.PopString(&state)) {
    OnSessionStateChange(state);
  } else {
    LOG(ERROR) << "Unable to read " << login_manager::kSessionStateChangedSignal
               << " args";
  }
}

void Daemon::HandleGetTpmStatusResponse(dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << cryptohome::kCryptohomeGetTpmStatus << " call failed";
    return;
  }

  cryptohome::BaseReply base_reply;
  dbus::MessageReader reader(response);
  if (!reader.PopArrayOfBytesAsProto(&base_reply)) {
    LOG(ERROR) << "Unable to parse " << cryptohome::kCryptohomeGetTpmStatus
               << "response";
    return;
  }
  if (base_reply.has_error()) {
    LOG(ERROR) << cryptohome::kCryptohomeGetTpmStatus << " response contains "
               << "error code " << base_reply.error();
    return;
  }
  if (!base_reply.HasExtension(cryptohome::GetTpmStatusReply::reply)) {
    LOG(ERROR) << cryptohome::kCryptohomeGetTpmStatus << " response doesn't "
               << "contain nested reply";
    return;
  }

  cryptohome::GetTpmStatusReply tpm_reply =
      base_reply.GetExtension(cryptohome::GetTpmStatusReply::reply);
  LOG(INFO) << "Received " << cryptohome::kCryptohomeGetTpmStatus
            << " response with dictionary attack count "
            << tpm_reply.dictionary_attack_counter();
  state_controller_->HandleTpmStatus(tpm_reply.dictionary_attack_counter());
}

std::unique_ptr<dbus::Response> Daemon::HandleRequestShutdownMethod(
    dbus::MethodCall* method_call) {
  // Both arguments are optional.
  dbus::MessageReader reader(method_call);
  int32_t arg = 0;
  ShutdownReason reason = ShutdownReason::OTHER_REQUEST_TO_POWERD;
  if (reader.PopInt32(&arg)) {
    switch (static_cast<RequestShutdownReason>(arg)) {
      case REQUEST_SHUTDOWN_FOR_USER:
        reason = ShutdownReason::USER_REQUEST;
        break;
      case REQUEST_SHUTDOWN_OTHER:
        reason = ShutdownReason::OTHER_REQUEST_TO_POWERD;
        break;
      default:
        LOG(WARNING) << "Got unknown shutdown reason " << arg;
    }
  }

  std::string description;
  reader.PopString(&description);

  LOG(INFO) << "Got " << kRequestShutdownMethod << " message from "
            << method_call->GetSender() << " with reason "
            << ShutdownReasonToString(reason) << " (" << description << ")";

  ShutDown(ShutdownMode::POWER_OFF, reason);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleRequestRestartMethod(
    dbus::MethodCall* method_call) {
  // Both arguments are optional.
  dbus::MessageReader reader(method_call);
  int32_t arg = 0;
  ShutdownReason reason = ShutdownReason::OTHER_REQUEST_TO_POWERD;
  if (reader.PopInt32(&arg)) {
    switch (static_cast<RequestRestartReason>(arg)) {
      case REQUEST_RESTART_FOR_USER:
        reason = ShutdownReason::USER_REQUEST;
        break;
      case REQUEST_RESTART_FOR_UPDATE:
        reason = ShutdownReason::SYSTEM_UPDATE;
        break;
      case REQUEST_RESTART_OTHER:
        reason = ShutdownReason::OTHER_REQUEST_TO_POWERD;
        break;
      default:
        LOG(WARNING) << "Got unknown restart reason " << arg;
    }
  }

  std::string description;
  reader.PopString(&description);

  LOG(INFO) << "Got " << kRequestRestartMethod << " message from "
            << method_call->GetSender() << " with reason "
            << ShutdownReasonToString(reason) << " (" << description << ")";

  ShutDown(ShutdownMode::REBOOT, reason);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleRequestSuspendMethod(
    dbus::MethodCall* method_call) {
  // Read an optional uint64_t argument specifying the wakeup count that is
  // expected.
  dbus::MessageReader reader(method_call);
  uint64_t external_wakeup_count = 0;
  const bool got_external_wakeup_count =
      reader.PopUint64(&external_wakeup_count);
  LOG(INFO) << "Got " << kRequestSuspendMethod << " message"
            << (got_external_wakeup_count
                    ? base::StringPrintf(" with external wakeup count %" PRIu64,
                                         external_wakeup_count)
                          .c_str()
                    : "")
            << " from " << method_call->GetSender();
  // Read an optional int32_t argument specifying the wakeup timeout for a
  // suspend request.
  int32_t wakeup_timeout = 0;
  reader.PopInt32(&wakeup_timeout);
  base::TimeDelta duration = base::TimeDelta::FromSeconds(wakeup_timeout);
  Suspend(SuspendImminent_Reason_OTHER, got_external_wakeup_count,
          external_wakeup_count, duration);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleVideoActivityMethod(
    dbus::MethodCall* method_call) {
  bool fullscreen = false;
  dbus::MessageReader reader(method_call);
  if (!reader.PopBool(&fullscreen))
    LOG(ERROR) << "Unable to read " << kHandleVideoActivityMethod << " args";

  video_activity_logger_->OnActivityReported();

  for (auto controller : all_backlight_controllers_)
    controller->HandleVideoActivity(fullscreen);
  state_controller_->HandleVideoActivity();
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleUserActivityMethod(
    dbus::MethodCall* method_call) {
  user_activity_logger_->OnActivityReported();

  int type_int = USER_ACTIVITY_OTHER;
  dbus::MessageReader reader(method_call);
  if (!reader.PopInt32(&type_int))
    LOG(ERROR) << "Unable to read " << kHandleUserActivityMethod << " args";
  UserActivityType type = static_cast<UserActivityType>(type_int);

  suspender_->HandleUserActivity();
  state_controller_->HandleUserActivity();
  for (auto controller : all_backlight_controllers_)
    controller->HandleUserActivity(type);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleWakeNotificationMethod(
    dbus::MethodCall* method_call) {
  suspender_->HandleWakeNotification();
  state_controller_->HandleWakeNotification();
  for (auto controller : all_backlight_controllers_)
    controller->HandleWakeNotification();
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleSetIsProjectingMethod(
    dbus::MethodCall* method_call) {
  bool is_projecting = false;
  dbus::MessageReader reader(method_call);
  if (!reader.PopBool(&is_projecting)) {
    LOG(ERROR) << "Unable to read " << kSetIsProjectingMethod << " args";
    return CreateInvalidArgsError(method_call, "Expected boolean state");
  }

  DisplayMode mode =
      is_projecting ? DisplayMode::PRESENTATION : DisplayMode::NORMAL;
  LOG(INFO) << "Chrome is using " << DisplayModeToString(mode)
            << " display mode";
  state_controller_->HandleDisplayModeChange(mode);
  suspender_->HandleDisplayModeChange(mode);
  input_device_controller_->SetDisplayMode(mode);
  for (auto controller : all_backlight_controllers_)
    controller->HandleDisplayModeChange(mode);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleSetPolicyMethod(
    dbus::MethodCall* method_call) {
  PowerManagementPolicy policy;
  dbus::MessageReader reader(method_call);
  if (!reader.PopArrayOfBytesAsProto(&policy)) {
    LOG(ERROR) << "Unable to parse " << kSetPolicyMethod << " request";
    return CreateInvalidArgsError(method_call, "Expected protobuf");
  }

  LOG(INFO) << "Received updated external policy: "
            << policy::StateController::GetPolicyDebugString(policy);
  state_controller_->HandlePolicyChange(policy);

  if (charge_controller_) {
    charge_controller_->HandlePolicyChange(policy);
  }

  for (auto controller : all_backlight_controllers_)
    controller->HandlePolicyChange(policy);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleSetBacklightsForcedOffMethod(
    dbus::MethodCall* method_call) {
  bool force_off = false;
  if (!dbus::MessageReader(method_call).PopBool(&force_off)) {
    LOG(ERROR) << "Unable to read " << kSetBacklightsForcedOffMethod << " args";
    return CreateInvalidArgsError(method_call, "Expected bool");
  }
  LOG(INFO) << "Received request to " << (force_off ? "start" : "stop")
            << " forcing backlights off";
  for (auto controller : all_backlight_controllers_)
    controller->SetForcedOff(force_off);
  return nullptr;
}

std::unique_ptr<dbus::Response> Daemon::HandleGetBacklightsForcedOffMethod(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> response(
      dbus::Response::FromMethodCall(method_call));

  // We can get the current state from any backlight controller.
  bool forced_off = all_backlight_controllers_.empty()
                        ? false
                        : all_backlight_controllers_.front()->GetForcedOff();
  dbus::MessageWriter(response.get()).AppendBool(forced_off);
  return response;
}

std::unique_ptr<dbus::Response> Daemon::HandleHasAmbientColorDeviceMethod(
    dbus::MethodCall* method_call) {
  std::unique_ptr<dbus::Response> response(
      dbus::Response::FromMethodCall(method_call));
  bool has_color_device = false;
  if (light_sensor_manager_)
    has_color_device = light_sensor_manager_->HasColorSensor();

  dbus::MessageWriter(response.get()).AppendBool(has_color_device);
  return response;
}

std::unique_ptr<dbus::Response> Daemon::HandleChangeWifiRegDomainMethod(
    dbus::MethodCall* method_call) {
  int32_t arg = 0;
  dbus::MessageReader reader(method_call);
  WifiRegDomain domain = WifiRegDomain::NONE;
  if (!reader.PopInt32(&arg)) {
    LOG(ERROR) << "Unable to read " << kChangeWifiRegDomainMethod << " args";
    return CreateInvalidArgsError(method_call, "Expected Int32");
  }
  switch (static_cast<WifiRegDomainDbus>(arg)) {
    case WIFI_REG_DOMAIN_FCC:
      domain = WifiRegDomain::FCC;
      break;
    case WIFI_REG_DOMAIN_EU:
      domain = WifiRegDomain::EU;
      break;
    case WIFI_REG_DOMAIN_REST_OF_WORLD:
      domain = WifiRegDomain::REST_OF_WORLD;
      break;
    case WIFI_REG_DOMAIN_NONE:
      break;
    default:
      LOG(WARNING) << "Got unknown WiFi regulatory domain " << arg;
  }

  LOG(INFO) << "Received request to change reg domain to \""
            << WifiRegDomainToString(domain) << "\"";
  wifi_controller_->HandleRegDomainChange(domain);
  return nullptr;
}

void Daemon::OnSessionStateChange(const std::string& state_str) {
  SessionState state = (state_str == kSessionStarted) ? SessionState::STARTED
                                                      : SessionState::STOPPED;
  if (state == session_state_)
    return;

  LOG(INFO) << "Session state changed to " << SessionStateToString(state);
  session_state_ = state;
  metrics_collector_->HandleSessionStateChange(state);
  state_controller_->HandleSessionStateChange(state);
  for (auto controller : all_backlight_controllers_)
    controller->HandleSessionStateChange(state);
}

void Daemon::RequestTpmStatus() {
  DCHECK(cryptohomed_dbus_proxy_);
  dbus::MethodCall method_call(cryptohome::kCryptohomeInterface,
                               cryptohome::kCryptohomeGetTpmStatus);
  dbus::MessageWriter writer(&method_call);
  writer.AppendProtoAsArrayOfBytes(cryptohome::GetTpmStatusRequest());
  dbus_wrapper_->CallMethodAsync(
      cryptohomed_dbus_proxy_, &method_call,
      base::TimeDelta::FromMilliseconds(kCryptohomedDBusTimeoutMs),
      base::Bind(&Daemon::HandleGetTpmStatusResponse,
                 weak_ptr_factory_.GetWeakPtr()));
}

void Daemon::ShutDown(ShutdownMode mode, ShutdownReason reason) {
  if (shutting_down_) {
    LOG(INFO) << "Shutdown already initiated; ignoring additional request";
    return;
  }

  std::string details;
  if (SuspendAndShutdownAreBlocked(&details)) {
    LOG(INFO) << "Postponing shutdown for lockfile(s): " << details;
    if (!retry_shutdown_for_lockfile_timer_.IsRunning()) {
      retry_shutdown_for_lockfile_timer_.Start(
          FROM_HERE, kShutdownLockfileRetryInterval,
          base::Bind(&Daemon::ShutDown, weak_ptr_factory_.GetWeakPtr(), mode,
                     reason));
    }
    return;
  }

  shutting_down_ = true;
  retry_shutdown_for_lockfile_timer_.Stop();
  suspender_->HandleShutdown();
  metrics_collector_->HandleShutdown(reason);

  for (auto controller : all_backlight_controllers_) {
    // If we're going to display a low-battery alert while shutting down, don't
    // turn the screen off immediately.
    if (!(reason == ShutdownReason::LOW_BATTERY &&
          controller == display_backlight_controller_.get()))
      controller->SetShuttingDown(true);
  }

  const std::string reason_str = ShutdownReasonToString(reason);
  switch (mode) {
    case ShutdownMode::POWER_OFF:
      LOG(INFO) << "Shutting down, reason: " << reason_str;
      RunSetuidHelper("shut_down", "--shutdown_reason=" + reason_str, false);
      break;
    case ShutdownMode::REBOOT:
      if (state_controller_->in_docked_mode()) {
        LOG(INFO) << "In docked mode, so telling EC to force lid open to avoid "
                  << "shutting down after reboot";
        RunSetuidHelper("set_force_lid_open", "--force_lid_open", true);
      }
      LOG(INFO) << "Restarting, reason: " << reason_str;
      RunSetuidHelper("reboot", "--shutdown_reason=" + reason_str, false);
      break;
  }
}

void Daemon::Suspend(SuspendImminent::Reason reason,
                     bool use_external_wakeup_count,
                     uint64_t external_wakeup_count,
                     base::TimeDelta duration) {
  if (shutting_down_) {
    LOG(INFO) << "Ignoring request for suspend with outstanding shutdown";
    return;
  }

  if (use_external_wakeup_count) {
    suspender_->RequestSuspendWithExternalWakeupCount(
        reason, external_wakeup_count, duration);
  } else {
    suspender_->RequestSuspend(reason, duration);
  }
}

void Daemon::SetBacklightsDimmedForInactivity(bool dimmed) {
  for (auto controller : all_backlight_controllers_)
    controller->SetDimmedForInactivity(dimmed);
  metrics_collector_->HandleScreenDimmedChange(
      dimmed, state_controller_->last_user_activity_time());
}

void Daemon::SetBacklightsOffForInactivity(bool off) {
  for (auto controller : all_backlight_controllers_)
    controller->SetOffForInactivity(off);
  metrics_collector_->HandleScreenOffChange(
      off, state_controller_->last_user_activity_time());
}

void Daemon::SetBacklightsSuspended(bool suspended) {
  for (auto controller : all_backlight_controllers_)
    controller->SetSuspended(suspended);
}

}  // namespace power_manager
