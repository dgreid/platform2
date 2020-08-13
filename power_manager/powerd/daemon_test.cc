// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/daemon.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <gtest/gtest.h>

#include "power_manager/common/battery_percentage_converter.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/common/metrics_sender_stub.h"
#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/daemon_delegate.h"
#include "power_manager/powerd/policy/backlight_controller_stub.h"
#include "power_manager/powerd/system/acpi_wakeup_helper_stub.h"
#include "power_manager/powerd/system/ambient_light_sensor_manager_stub.h"
#include "power_manager/powerd/system/audio_client_stub.h"
#include "power_manager/powerd/system/backlight_stub.h"
#include "power_manager/powerd/system/charge_controller_helper_stub.h"
#include "power_manager/powerd/system/cros_ec_helper_stub.h"
#include "power_manager/powerd/system/dark_resume_stub.h"
#include "power_manager/powerd/system/dbus_wrapper_stub.h"
#include "power_manager/powerd/system/display/display_power_setter_stub.h"
#include "power_manager/powerd/system/display/display_watcher_stub.h"
#include "power_manager/powerd/system/input_watcher_stub.h"
#include "power_manager/powerd/system/lockfile_checker_stub.h"
#include "power_manager/powerd/system/peripheral_battery_watcher.h"
#include "power_manager/powerd/system/power_supply.h"
#include "power_manager/powerd/system/power_supply_stub.h"
#include "power_manager/powerd/system/suspend_configurator_stub.h"
#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/powerd/system/user_proximity_watcher_stub.h"
#include "power_manager/proto_bindings/backlight.pb.h"
#include "power_manager/proto_bindings/switch_states.pb.h"

namespace power_manager {

class DaemonTest : public ::testing::Test, public DaemonDelegate {
 public:
  // The hardcoded constants here are arbitrary and not used by Daemon.
  DaemonTest()
      : passed_prefs_(new FakePrefs()),
        passed_dbus_wrapper_(new system::DBusWrapperStub()),
        passed_udev_(new system::UdevStub()),
        passed_ambient_light_sensor_manager_(
            new system::AmbientLightSensorManagerStub()),
        passed_display_watcher_(new system::DisplayWatcherStub()),
        passed_display_power_setter_(new system::DisplayPowerSetterStub()),
        passed_internal_backlight_(new system::BacklightStub(
            100, 100, system::BacklightInterface::BrightnessScale::kUnknown)),
        passed_keyboard_backlight_(new system::BacklightStub(
            100, 100, system::BacklightInterface::BrightnessScale::kUnknown)),
        passed_external_backlight_controller_(
            new policy::BacklightControllerStub()),
        passed_internal_backlight_controller_(
            new policy::BacklightControllerStub()),
        passed_keyboard_backlight_controller_(
            new policy::BacklightControllerStub()),
        passed_input_watcher_(new system::InputWatcherStub()),
        passed_acpi_wakeup_helper_(new system::AcpiWakeupHelperStub()),
        passed_ec_helper_(new system::CrosEcHelperStub()),
        passed_power_supply_(new system::PowerSupplyStub()),
        passed_sar_watcher_(new system::UserProximityWatcherStub()),
        passed_dark_resume_(new system::DarkResumeStub()),
        passed_audio_client_(new system::AudioClientStub()),
        passed_lockfile_checker_(new system::LockfileCheckerStub()),
        passed_metrics_sender_(new MetricsSenderStub()),
        passed_charge_controller_helper_(
            new system::ChargeControllerHelperStub()),
        passed_suspend_configurator_(new system::SuspendConfiguratorStub()),
        prefs_(passed_prefs_.get()),
        dbus_wrapper_(passed_dbus_wrapper_.get()),
        udev_(passed_udev_.get()),
        ambient_light_sensor_manager_(
            passed_ambient_light_sensor_manager_.get()),
        display_watcher_(passed_display_watcher_.get()),
        display_power_setter_(passed_display_power_setter_.get()),
        internal_backlight_(passed_internal_backlight_.get()),
        keyboard_backlight_(passed_keyboard_backlight_.get()),
        external_backlight_controller_(
            passed_external_backlight_controller_.get()),
        internal_backlight_controller_(
            passed_internal_backlight_controller_.get()),
        keyboard_backlight_controller_(
            passed_keyboard_backlight_controller_.get()),
        input_watcher_(passed_input_watcher_.get()),
        acpi_wakeup_helper_(passed_acpi_wakeup_helper_.get()),
        ec_helper_(passed_ec_helper_.get()),
        power_supply_(passed_power_supply_.get()),
        sar_watcher_(passed_sar_watcher_.get()),
        dark_resume_(passed_dark_resume_.get()),
        audio_client_(passed_audio_client_.get()),
        lockfile_checker_(passed_lockfile_checker_.get()),
        metrics_sender_(passed_metrics_sender_.get()),
        pid_(2) {
    CHECK(run_dir_.CreateUniqueTempDir());
    CHECK(run_dir_.IsValid());

    CHECK(temp_dir_.CreateUniqueTempDir());
    CHECK(temp_dir_.IsValid());
    wakeup_count_path_ = temp_dir_.GetPath().Append("wakeup_count");
    oobe_completed_path_ = temp_dir_.GetPath().Append("oobe_completed");
    suspended_state_path_ = temp_dir_.GetPath().Append("suspended_state");
    flashrom_lock_path_ = temp_dir_.GetPath().Append("flashrom_lock");
    battery_tool_lock_path_ = temp_dir_.GetPath().Append("battery_tool_lock");
    proc_path_ = temp_dir_.GetPath().Append("proc");
  }
  ~DaemonTest() override {}

  void Init() {
    // These prefs are required by policy::Suspender.
    prefs_->SetInt64(kRetrySuspendMsPref, 10000);
    prefs_->SetInt64(kRetrySuspendAttemptsPref, 10);

    // These prefs are required by policy::StateController.
    prefs_->SetInt64(kPluggedSuspendMsPref, 1800000);
    prefs_->SetInt64(kPluggedOffMsPref, 480000);
    prefs_->SetInt64(kPluggedDimMsPref, 420000);
    prefs_->SetInt64(kUnpluggedSuspendMsPref, 600000);
    prefs_->SetInt64(kUnpluggedOffMsPref, 360000);
    prefs_->SetInt64(kUnpluggedDimMsPref, 300000);

    daemon_.reset(new Daemon(this, run_dir_.GetPath()));
    daemon_->set_wakeup_count_path_for_testing(wakeup_count_path_);
    daemon_->set_oobe_completed_path_for_testing(oobe_completed_path_);
    daemon_->set_suspended_state_path_for_testing(suspended_state_path_);
    daemon_->Init();
  }

  // DaemonDelegate:
  std::unique_ptr<PrefsInterface> CreatePrefs() override {
    return std::move(passed_prefs_);
  }
  std::unique_ptr<system::DBusWrapperInterface> CreateDBusWrapper() override {
    return std::move(passed_dbus_wrapper_);
  }
  std::unique_ptr<system::UdevInterface> CreateUdev() override {
    return std::move(passed_udev_);
  }
  std::unique_ptr<system::AmbientLightSensorManagerInterface>
  CreateAmbientLightSensorManager(PrefsInterface* prefs) override {
    return std::move(passed_ambient_light_sensor_manager_);
  }
  std::unique_ptr<system::DisplayWatcherInterface> CreateDisplayWatcher(
      system::UdevInterface* udev) override {
    EXPECT_EQ(udev_, udev);
    return std::move(passed_display_watcher_);
  }
  std::unique_ptr<system::DisplayPowerSetterInterface> CreateDisplayPowerSetter(
      system::DBusWrapperInterface* dbus_wrapper) override {
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    return std::move(passed_display_power_setter_);
  }
  std::unique_ptr<policy::BacklightController>
  CreateExternalBacklightController(
      system::DisplayWatcherInterface* display_watcher,
      system::DisplayPowerSetterInterface* display_power_setter,
      system::DBusWrapperInterface* dbus_wrapper) override {
    EXPECT_EQ(display_watcher_, display_watcher);
    EXPECT_EQ(display_power_setter_, display_power_setter);
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    return std::move(passed_external_backlight_controller_);
  }
  std::unique_ptr<system::BacklightInterface> CreateInternalBacklight(
      const base::FilePath& base_path, const std::string& pattern) override {
    // This should only be called for the display backlight.
    EXPECT_EQ(kInternalBacklightPath, base_path.value());
    EXPECT_EQ(kInternalBacklightPattern, pattern);
    return std::move(passed_internal_backlight_);
  }
  std::unique_ptr<system::BacklightInterface> CreatePluggableInternalBacklight(
      system::UdevInterface* udev,
      const std::string& udev_subsystem,
      const base::FilePath& base_path,
      const std::string& pattern) override {
    // This should only be called for the keyboard backlight.
    EXPECT_EQ(udev_, udev);
    EXPECT_EQ(kKeyboardBacklightUdevSubsystem, udev_subsystem);
    EXPECT_EQ(kKeyboardBacklightPath, base_path.value());
    EXPECT_EQ(kKeyboardBacklightPattern, pattern);
    return std::move(passed_keyboard_backlight_);
  }
  std::unique_ptr<policy::BacklightController>
  CreateInternalBacklightController(
      system::BacklightInterface* backlight,
      PrefsInterface* prefs,
      system::AmbientLightSensorInterface* sensor,
      system::DisplayPowerSetterInterface* power_setter,
      system::DBusWrapperInterface* dbus_wrapper,
      LidState initial_lid_state) override {
    EXPECT_EQ(internal_backlight_, backlight);
    EXPECT_EQ(prefs_, prefs);
    EXPECT_TRUE(
        !sensor ||
        sensor ==
            ambient_light_sensor_manager_->GetSensorForInternalBacklight());
    EXPECT_EQ(display_power_setter_, power_setter);
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    EXPECT_EQ(input_watcher_->QueryLidState(), initial_lid_state);
    return std::move(passed_internal_backlight_controller_);
  }
  std::unique_ptr<policy::BacklightController>
  CreateKeyboardBacklightController(
      system::BacklightInterface* backlight,
      PrefsInterface* prefs,
      system::AmbientLightSensorInterface* sensor,
      system::DBusWrapperInterface* dbus_wrapper,
      policy::BacklightController* display_backlight_controller,
      LidState initial_lid_state,
      TabletMode initial_tablet_mode) override {
    EXPECT_EQ(keyboard_backlight_, backlight);
    EXPECT_EQ(prefs_, prefs);
    EXPECT_TRUE(
        !sensor ||
        sensor ==
            ambient_light_sensor_manager_->GetSensorForKeyboardBacklight());
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    EXPECT_EQ(internal_backlight_controller_, display_backlight_controller);
    EXPECT_EQ(input_watcher_->QueryLidState(), initial_lid_state);
    EXPECT_EQ(input_watcher_->GetTabletMode(), initial_tablet_mode);
    return std::move(passed_keyboard_backlight_controller_);
  }
  std::unique_ptr<system::InputWatcherInterface> CreateInputWatcher(
      PrefsInterface* prefs, system::UdevInterface* udev) override {
    EXPECT_EQ(prefs_, prefs);
    EXPECT_EQ(udev_, udev);
    return std::move(passed_input_watcher_);
  }
  std::unique_ptr<system::AcpiWakeupHelperInterface> CreateAcpiWakeupHelper()
      override {
    return std::move(passed_acpi_wakeup_helper_);
  }
  std::unique_ptr<system::CrosEcHelperInterface> CreateCrosEcHelper() override {
    return std::move(passed_ec_helper_);
  }
  std::unique_ptr<system::PeripheralBatteryWatcher>
  CreatePeripheralBatteryWatcher(system::DBusWrapperInterface* dbus_wrapper,
                                 system::UdevInterface* udev) override {
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    EXPECT_EQ(udev_, udev);
    return nullptr;
  }
  std::unique_ptr<system::PowerSupplyInterface> CreatePowerSupply(
      const base::FilePath& power_supply_path,
      PrefsInterface* prefs,
      system::UdevInterface* udev,
      system::DBusWrapperInterface* dbus_wrapper,
      BatteryPercentageConverter* battery_percentage_converter) override {
    EXPECT_EQ(kPowerStatusPath, power_supply_path.value());
    EXPECT_EQ(prefs_, prefs);
    EXPECT_EQ(udev_, udev);
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    EXPECT_TRUE(battery_percentage_converter);
    return std::move(passed_power_supply_);
  }
  std::unique_ptr<system::UserProximityWatcherInterface>
  CreateUserProximityWatcher(PrefsInterface* prefs,
                             system::UdevInterface* udev) override {
    EXPECT_EQ(prefs_, prefs);
    EXPECT_EQ(udev_, udev);
    return std::move(passed_sar_watcher_);
  }
  std::unique_ptr<system::DarkResumeInterface> CreateDarkResume(
      PrefsInterface* prefs,
      system::WakeupSourceIdentifierInterface* wakeup_source_identifier)
      override {
    EXPECT_EQ(prefs_, prefs);
    return std::move(passed_dark_resume_);
  }
  std::unique_ptr<system::AudioClientInterface> CreateAudioClient(
      system::DBusWrapperInterface* dbus_wrapper,
      const base::FilePath& run_dir) override {
    EXPECT_EQ(dbus_wrapper_, dbus_wrapper);
    return std::move(passed_audio_client_);
  }
  std::unique_ptr<system::LockfileCheckerInterface> CreateLockfileChecker(
      const base::FilePath& dir,
      const std::vector<base::FilePath>& files) override {
    return std::move(passed_lockfile_checker_);
  }
  std::unique_ptr<MetricsSenderInterface> CreateMetricsSender() override {
    return std::move(passed_metrics_sender_);
  }
  std::unique_ptr<system::ChargeControllerHelperInterface>
  CreateChargeControllerHelper() override {
    return std::move(passed_charge_controller_helper_);
  }
  std::unique_ptr<system::SuspendConfiguratorInterface>
  CreateSuspendConfigurator(PrefsInterface* prefs) override {
    EXPECT_EQ(prefs_, prefs);
    return std::move(passed_suspend_configurator_);
  }

  pid_t GetPid() override { return pid_; }
  void Launch(const std::string& command) override {
    async_commands_.push_back(command);
  }
  int Run(const std::string& command) override {
    sync_commands_.push_back(command);
    return 0;
  }

 protected:
  // Send the appropriate events to put StateController into docked mode.
  void EnterDockedMode() {
    dbus::MethodCall call(kPowerManagerInterface, kSetIsProjectingMethod);
    dbus::MessageWriter(&call).AppendBool(true /* is_projecting */);
    ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&call).get());

    input_watcher_->set_lid_state(LidState::CLOSED);
    input_watcher_->NotifyObserversAboutLidState();
  }

  // Returns the command that Daemon should execute to shut down for a given
  // reason.
  std::string GetShutdownCommand(ShutdownReason reason) {
    return base::StringPrintf("%s --action=shut_down --shutdown_reason=%s",
                              kSetuidHelperPath,
                              ShutdownReasonToString(reason).c_str());
  }

  // Commands for forcing the lid open or stopping forcing it open.
  const std::string kForceLidOpenCommand =
      std::string(kSetuidHelperPath) +
      " --action=set_force_lid_open --force_lid_open";
  const std::string kNoForceLidOpenCommand =
      std::string(kSetuidHelperPath) +
      " --action=set_force_lid_open --noforce_lid_open";

  // Stub objects to be transferred by Create* methods.
  std::unique_ptr<FakePrefs> passed_prefs_;
  std::unique_ptr<system::DBusWrapperStub> passed_dbus_wrapper_;
  std::unique_ptr<system::UdevStub> passed_udev_;
  std::unique_ptr<system::AmbientLightSensorManagerStub>
      passed_ambient_light_sensor_manager_;
  std::unique_ptr<system::DisplayWatcherStub> passed_display_watcher_;
  std::unique_ptr<system::DisplayPowerSetterStub> passed_display_power_setter_;
  std::unique_ptr<system::BacklightStub> passed_internal_backlight_;
  std::unique_ptr<system::BacklightStub> passed_keyboard_backlight_;
  std::unique_ptr<policy::BacklightControllerStub>
      passed_external_backlight_controller_;
  std::unique_ptr<policy::BacklightControllerStub>
      passed_internal_backlight_controller_;
  std::unique_ptr<policy::BacklightControllerStub>
      passed_keyboard_backlight_controller_;
  std::unique_ptr<system::InputWatcherStub> passed_input_watcher_;
  std::unique_ptr<system::AcpiWakeupHelperStub> passed_acpi_wakeup_helper_;
  std::unique_ptr<system::CrosEcHelperStub> passed_ec_helper_;
  std::unique_ptr<system::PowerSupplyStub> passed_power_supply_;
  std::unique_ptr<system::UserProximityWatcherStub> passed_sar_watcher_;
  std::unique_ptr<system::DarkResumeStub> passed_dark_resume_;
  std::unique_ptr<system::AudioClientStub> passed_audio_client_;
  std::unique_ptr<system::LockfileCheckerStub> passed_lockfile_checker_;
  std::unique_ptr<MetricsSenderStub> passed_metrics_sender_;
  std::unique_ptr<system::ChargeControllerHelperInterface>
      passed_charge_controller_helper_;
  std::unique_ptr<system::SuspendConfiguratorInterface>
      passed_suspend_configurator_;

  // Pointers to objects originally stored in |passed_*| members. These allow
  // continued access by tests even after the corresponding Create* method has
  // been called and ownership has been transferred to |daemon_|.
  FakePrefs* prefs_;
  system::DBusWrapperStub* dbus_wrapper_;
  system::UdevStub* udev_;
  system::AmbientLightSensorManagerStub* ambient_light_sensor_manager_;
  system::DisplayWatcherStub* display_watcher_;
  system::DisplayPowerSetterStub* display_power_setter_;
  system::BacklightStub* internal_backlight_;
  system::BacklightStub* keyboard_backlight_;
  policy::BacklightControllerStub* external_backlight_controller_;
  policy::BacklightControllerStub* internal_backlight_controller_;
  policy::BacklightControllerStub* keyboard_backlight_controller_;
  system::InputWatcherStub* input_watcher_;
  system::AcpiWakeupHelperStub* acpi_wakeup_helper_;
  system::CrosEcHelperStub* ec_helper_;
  system::PowerSupplyStub* power_supply_;
  system::UserProximityWatcherStub* sar_watcher_;
  system::DarkResumeStub* dark_resume_;
  system::AudioClientStub* audio_client_;
  system::LockfileCheckerStub* lockfile_checker_;
  MetricsSenderStub* metrics_sender_;

  // Run directory passed to |daemon_|.
  base::ScopedTempDir run_dir_;

  // Temp files passed to |daemon_|.
  base::ScopedTempDir temp_dir_;
  base::FilePath wakeup_count_path_;
  base::FilePath oobe_completed_path_;
  base::FilePath suspended_state_path_;
  base::FilePath flashrom_lock_path_;
  base::FilePath battery_tool_lock_path_;
  base::FilePath proc_path_;

  // Value to return from GetPid().
  pid_t pid_;

  // Command lines executed via Launch() and Run(), respectively.
  std::vector<std::string> async_commands_;
  std::vector<std::string> sync_commands_;

  std::unique_ptr<Daemon> daemon_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DaemonTest);
};

TEST_F(DaemonTest, NotifyMembersAboutEvents) {
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);

  Init();
  internal_backlight_controller_->ResetStats();
  keyboard_backlight_controller_->ResetStats();

  // Power button events.
  input_watcher_->NotifyObserversAboutPowerButtonEvent(ButtonState::DOWN);
  EXPECT_EQ(1, internal_backlight_controller_->power_button_presses());
  EXPECT_EQ(1, keyboard_backlight_controller_->power_button_presses());

  // Hover state changes.
  input_watcher_->NotifyObserversAboutHoverState(true);
  input_watcher_->NotifyObserversAboutHoverState(false);
  ASSERT_EQ(2, internal_backlight_controller_->hover_state_changes().size());
  EXPECT_TRUE(internal_backlight_controller_->hover_state_changes()[0]);
  EXPECT_FALSE(internal_backlight_controller_->hover_state_changes()[1]);
  ASSERT_EQ(2, keyboard_backlight_controller_->hover_state_changes().size());
  EXPECT_TRUE(keyboard_backlight_controller_->hover_state_changes()[0]);
  EXPECT_FALSE(keyboard_backlight_controller_->hover_state_changes()[1]);

  // Lid events.
  input_watcher_->set_lid_state(LidState::CLOSED);
  input_watcher_->NotifyObserversAboutLidState();
  ASSERT_EQ(1, internal_backlight_controller_->lid_state_changes().size());
  EXPECT_EQ(LidState::CLOSED,
            internal_backlight_controller_->lid_state_changes()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->lid_state_changes().size());
  EXPECT_EQ(LidState::CLOSED,
            keyboard_backlight_controller_->lid_state_changes()[0]);

  // Tablet mode changes.
  input_watcher_->set_tablet_mode(TabletMode::ON);
  input_watcher_->NotifyObserversAboutTabletMode();
  ASSERT_EQ(1, internal_backlight_controller_->tablet_mode_changes().size());
  EXPECT_EQ(TabletMode::ON,
            internal_backlight_controller_->tablet_mode_changes()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->tablet_mode_changes().size());
  EXPECT_EQ(TabletMode::ON,
            keyboard_backlight_controller_->tablet_mode_changes()[0]);

  // Power source changes.
  system::PowerStatus status;
  status.line_power_on = true;
  power_supply_->set_status(status);
  power_supply_->NotifyObservers();
  ASSERT_EQ(1, internal_backlight_controller_->power_source_changes().size());
  EXPECT_EQ(PowerSource::AC,
            internal_backlight_controller_->power_source_changes()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->power_source_changes().size());
  EXPECT_EQ(PowerSource::AC,
            keyboard_backlight_controller_->power_source_changes()[0]);

  // User activity reports.
  dbus::MethodCall user_call(kPowerManagerInterface, kHandleUserActivityMethod);
  dbus::MessageWriter(&user_call)
      .AppendInt32(USER_ACTIVITY_BRIGHTNESS_UP_KEY_PRESS);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&user_call).get());
  ASSERT_EQ(1, internal_backlight_controller_->user_activity_reports().size());
  EXPECT_EQ(USER_ACTIVITY_BRIGHTNESS_UP_KEY_PRESS,
            internal_backlight_controller_->user_activity_reports()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->user_activity_reports().size());
  EXPECT_EQ(USER_ACTIVITY_BRIGHTNESS_UP_KEY_PRESS,
            keyboard_backlight_controller_->user_activity_reports()[0]);

  // Video activity reports.
  dbus::MethodCall video_call(kPowerManagerInterface,
                              kHandleVideoActivityMethod);
  dbus::MessageWriter(&video_call).AppendBool(true /* fullscreen */);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&video_call).get());
  ASSERT_EQ(1, internal_backlight_controller_->video_activity_reports().size());
  EXPECT_EQ(true, internal_backlight_controller_->video_activity_reports()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->video_activity_reports().size());
  EXPECT_EQ(true, keyboard_backlight_controller_->video_activity_reports()[0]);

  // Display mode / projecting changes.
  dbus::MethodCall display_call(kPowerManagerInterface, kSetIsProjectingMethod);
  dbus::MessageWriter(&display_call).AppendBool(true /* is_projecting */);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&display_call).get());
  ASSERT_EQ(1, internal_backlight_controller_->display_mode_changes().size());
  EXPECT_EQ(DisplayMode::PRESENTATION,
            internal_backlight_controller_->display_mode_changes()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->display_mode_changes().size());
  EXPECT_EQ(DisplayMode::PRESENTATION,
            keyboard_backlight_controller_->display_mode_changes()[0]);

  // Policy updates.
  dbus::MethodCall policy_call(kPowerManagerInterface, kSetPolicyMethod);
  PowerManagementPolicy policy;
  const char kPolicyReason[] = "foo";
  policy.set_reason(kPolicyReason);
  dbus::MessageWriter(&policy_call).AppendProtoAsArrayOfBytes(policy);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&policy_call).get());
  ASSERT_EQ(1, internal_backlight_controller_->policy_changes().size());
  EXPECT_EQ(kPolicyReason,
            internal_backlight_controller_->policy_changes()[0].reason());
  ASSERT_EQ(1, keyboard_backlight_controller_->policy_changes().size());
  EXPECT_EQ(kPolicyReason,
            keyboard_backlight_controller_->policy_changes()[0].reason());

  // Session state changes.
  dbus::Signal session_signal(login_manager::kSessionManagerInterface,
                              login_manager::kSessionStateChangedSignal);
  dbus::MessageWriter(&session_signal).AppendString("started");
  dbus_wrapper_->EmitRegisteredSignal(
      dbus_wrapper_->GetObjectProxy(login_manager::kSessionManagerServiceName,
                                    login_manager::kSessionManagerServicePath),
      &session_signal);
  ASSERT_EQ(1, internal_backlight_controller_->session_state_changes().size());
  EXPECT_EQ(SessionState::STARTED,
            internal_backlight_controller_->session_state_changes()[0]);
  ASSERT_EQ(1, keyboard_backlight_controller_->session_state_changes().size());
  EXPECT_EQ(SessionState::STARTED,
            keyboard_backlight_controller_->session_state_changes()[0]);

  // Chrome restarts.
  dbus_wrapper_->NotifyNameOwnerChanged(chromeos::kDisplayServiceName, "old",
                                        "new");
  dbus_wrapper_->NotifyNameOwnerChanged(chromeos::kDisplayServiceName, "new",
                                        "newer");
  EXPECT_EQ(2, internal_backlight_controller_->display_service_starts());
  EXPECT_EQ(2, keyboard_backlight_controller_->display_service_starts());

  // Wake notification events.
  dbus::MethodCall wake_notification_call(kPowerManagerInterface,
                                          kHandleWakeNotificationMethod);
  ASSERT_TRUE(
      dbus_wrapper_->CallExportedMethodSync(&wake_notification_call).get());
  ASSERT_EQ(1, internal_backlight_controller_->wake_notification_reports());
}

TEST_F(DaemonTest, DontReportTabletModeChangeFromInit) {
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);
  input_watcher_->set_tablet_mode(TabletMode::ON);
  Init();

  // The initial tablet mode is already passed to
  // CreateKeyboardBacklightController(), so Init() shouldn't send an extra
  // notification about it changing.
  EXPECT_EQ(0, internal_backlight_controller_->tablet_mode_changes().size());
  EXPECT_EQ(0, keyboard_backlight_controller_->tablet_mode_changes().size());
}

TEST_F(DaemonTest, ForceBacklightsOff) {
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);
  Init();

  // Tell Daemon to force the backlights off.
  dbus::MethodCall set_off_call(kPowerManagerInterface,
                                kSetBacklightsForcedOffMethod);
  dbus::MessageWriter(&set_off_call).AppendBool(true);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&set_off_call).get());
  EXPECT_TRUE(internal_backlight_controller_->forced_off());
  EXPECT_TRUE(keyboard_backlight_controller_->forced_off());

  dbus::MethodCall get_call(kPowerManagerInterface,
                            kGetBacklightsForcedOffMethod);
  auto response = dbus_wrapper_->CallExportedMethodSync(&get_call);
  ASSERT_TRUE(response.get());
  bool forced_off = false;
  ASSERT_TRUE(dbus::MessageReader(response.get()).PopBool(&forced_off));
  EXPECT_TRUE(forced_off);

  // Now stop forcing them off.
  dbus::MethodCall set_on_call(kPowerManagerInterface,
                               kSetBacklightsForcedOffMethod);
  dbus::MessageWriter(&set_on_call).AppendBool(false);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&set_on_call).get());
  EXPECT_FALSE(internal_backlight_controller_->forced_off());
  EXPECT_FALSE(keyboard_backlight_controller_->forced_off());

  response = dbus_wrapper_->CallExportedMethodSync(&get_call);
  ASSERT_TRUE(response.get());
  ASSERT_TRUE(dbus::MessageReader(response.get()).PopBool(&forced_off));
  EXPECT_FALSE(forced_off);
}

TEST_F(DaemonTest, RequestShutdown) {
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);
  Init();

  async_commands_.clear();
  sync_commands_.clear();
  dbus::MethodCall method_call(kPowerManagerInterface, kRequestShutdownMethod);
  dbus::MessageWriter message_writer(&method_call);
  message_writer.AppendInt32(REQUEST_SHUTDOWN_FOR_USER);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&method_call).get());

  EXPECT_TRUE(internal_backlight_controller_->shutting_down());
  EXPECT_TRUE(keyboard_backlight_controller_->shutting_down());

  EXPECT_EQ(0, sync_commands_.size());
  ASSERT_EQ(1, async_commands_.size());
  EXPECT_EQ(GetShutdownCommand(ShutdownReason::USER_REQUEST),
            async_commands_[0]);

  // Sending another request shouldn't do anything.
  async_commands_.clear();
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&method_call).get());
  EXPECT_EQ(0, async_commands_.size());
}

TEST_F(DaemonTest, RequestRestart) {
  Init();

  async_commands_.clear();
  dbus::MethodCall method_call(kPowerManagerInterface, kRequestRestartMethod);
  dbus::MessageWriter message_writer(&method_call);
  message_writer.AppendInt32(REQUEST_RESTART_FOR_UPDATE);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&method_call).get());

  ASSERT_EQ(1, async_commands_.size());
  EXPECT_EQ(base::StringPrintf(
                "%s --action=reboot --shutdown_reason=%s", kSetuidHelperPath,
                ShutdownReasonToString(ShutdownReason::SYSTEM_UPDATE).c_str()),
            async_commands_[0]);
}

TEST_F(DaemonTest, ShutDownForLowBattery) {
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);
  Init();

  // We shouldn't shut down if the battery isn't below the threshold.
  async_commands_.clear();
  system::PowerStatus status;
  status.battery_is_present = true;
  status.battery_below_shutdown_threshold = false;
  power_supply_->set_status(status);
  power_supply_->NotifyObservers();
  EXPECT_EQ(0, async_commands_.size());

  // Now drop below the threshold.
  async_commands_.clear();
  status.battery_below_shutdown_threshold = true;
  power_supply_->set_status(status);
  power_supply_->NotifyObservers();

  // Keep the display backlight on so we can show a low-battery alert.
  EXPECT_FALSE(internal_backlight_controller_->shutting_down());
  EXPECT_TRUE(keyboard_backlight_controller_->shutting_down());

  ASSERT_EQ(1, async_commands_.size());
  EXPECT_EQ(GetShutdownCommand(ShutdownReason::LOW_BATTERY),
            async_commands_[0]);
}

TEST_F(DaemonTest, DeferShutdownWhileFlashromRunning) {
  Init();
  async_commands_.clear();

  // The system should stay up if a lockfile exists.
  lockfile_checker_->set_files_to_return(
      {temp_dir_.GetPath().Append("lockfile")});
  dbus::MethodCall method_call(kPowerManagerInterface, kRequestShutdownMethod);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&method_call).get());
  EXPECT_EQ(0, async_commands_.size());

  // It should still be up after the retry timer fires.
  ASSERT_TRUE(daemon_->TriggerRetryShutdownTimerForTesting());
  EXPECT_EQ(0, async_commands_.size());

  // Now remove the lockfile. The next time the timer fires, Daemon should start
  // shutting down.
  lockfile_checker_->set_files_to_return({});
  ASSERT_TRUE(daemon_->TriggerRetryShutdownTimerForTesting());
  ASSERT_EQ(1, async_commands_.size());
  EXPECT_EQ(GetShutdownCommand(ShutdownReason::OTHER_REQUEST_TO_POWERD),
            async_commands_[0]);

  // The timer should've been stopped.
  EXPECT_FALSE(daemon_->TriggerRetryShutdownTimerForTesting());
}

TEST_F(DaemonTest, ForceLidOpenForDockedModeReboot) {
  // During initialization, we should always stop forcing the lid open to undo
  // a force request that might've been sent earlier.
  prefs_->SetInt64(kUseLidPref, 1);
  Init();
  ASSERT_EQ(1, async_commands_.size());
  EXPECT_EQ(kNoForceLidOpenCommand, async_commands_[0]);

  // We should synchronously force the lid open before rebooting.
  async_commands_.clear();
  EnterDockedMode();
  dbus::MethodCall call(kPowerManagerInterface, kRequestRestartMethod);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&call).get());
  ASSERT_EQ(1, sync_commands_.size());
  EXPECT_EQ(kForceLidOpenCommand, sync_commands_[0]);
}

TEST_F(DaemonTest, DontForceLidOpenForDockedModeShutdown) {
  // When shutting down in docked mode, we shouldn't force the lid open.
  prefs_->SetInt64(kUseLidPref, 1);
  Init();
  async_commands_.clear();
  EnterDockedMode();
  dbus::MethodCall call(kPowerManagerInterface, kRequestShutdownMethod);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&call).get());
  EXPECT_EQ(0, sync_commands_.size());
}

TEST_F(DaemonTest, DontForceLidOpenForNormalReboot) {
  // When rebooting outside of docked mode, we shouldn't force the lid open.
  prefs_->SetInt64(kUseLidPref, 1);
  Init();
  dbus::MethodCall call(kPowerManagerInterface, kRequestRestartMethod);
  ASSERT_TRUE(dbus_wrapper_->CallExportedMethodSync(&call).get());
  EXPECT_EQ(0, sync_commands_.size());
}

TEST_F(DaemonTest, DontResetForceLidOpenWhenNotUsingLid) {
  // When starting while configured to not use the lid, powerd shouldn't stop
  // forcing the lid open. This lets developers tell the EC to force the lid
  // open without having powerd continually undo their setting whenever they
  // reboot.
  prefs_->SetInt64(kUseLidPref, 0);
  Init();
  EXPECT_EQ(0, async_commands_.size());
}

TEST_F(DaemonTest, FirstRunAfterBootWhenTrue) {
  const base::FilePath already_ran_path =
      run_dir_.GetPath().Append(Daemon::kAlreadyRanFileName);
  Init();
  EXPECT_TRUE(daemon_->first_run_after_boot_for_testing());
  EXPECT_TRUE(base::PathExists(already_ran_path));
}

TEST_F(DaemonTest, FirstRunAfterBootWhenFalse) {
  const base::FilePath already_ran_path =
      run_dir_.GetPath().Append(Daemon::kAlreadyRanFileName);

  ASSERT_EQ(base::WriteFile(already_ran_path, nullptr, 0), 0);
  Init();
  EXPECT_FALSE(daemon_->first_run_after_boot_for_testing());
  EXPECT_TRUE(base::PathExists(already_ran_path));
}

TEST_F(DaemonTest, FactoryMode) {
  prefs_->SetInt64(kFactoryModePref, 1);
  prefs_->SetInt64(kUseLidPref, 1);
  prefs_->SetInt64(kHasAmbientLightSensorPref, 1);
  prefs_->SetInt64(kHasKeyboardBacklightPref, 1);

  Init();

  // kNoForceLidOpenCommand shouldn't be executed at startup in factory mode.
  EXPECT_EQ(std::vector<std::string>(), async_commands_);

  // Check that Daemon didn't initialize most objects related to adjusting the
  // display or keyboard backlights.
  EXPECT_TRUE(passed_ambient_light_sensor_manager_);
  EXPECT_TRUE(passed_internal_backlight_);
  EXPECT_TRUE(passed_keyboard_backlight_);
  EXPECT_TRUE(passed_external_backlight_controller_);
  EXPECT_TRUE(passed_internal_backlight_controller_);
  EXPECT_TRUE(passed_keyboard_backlight_controller_);

  // The initial display power still needs to be set after Chrome's display
  // service comes up, though: http://b/78436034
  EXPECT_EQ(0, display_power_setter_->num_power_calls());
  dbus_wrapper_->NotifyNameOwnerChanged(chromeos::kDisplayServiceName, "", "1");
  EXPECT_EQ(1, display_power_setter_->num_power_calls());
  EXPECT_EQ(chromeos::DISPLAY_POWER_ALL_ON, display_power_setter_->state());
  EXPECT_EQ(base::TimeDelta(), display_power_setter_->delay());

  // Display- and keyboard-backlight-related D-Bus methods shouldn't be
  // exported.
  EXPECT_FALSE(dbus_wrapper_->IsMethodExported(kSetScreenBrightnessMethod));
  EXPECT_FALSE(
      dbus_wrapper_->IsMethodExported(kIncreaseScreenBrightnessMethod));
  EXPECT_FALSE(
      dbus_wrapper_->IsMethodExported(kDecreaseScreenBrightnessMethod));
  EXPECT_FALSE(
      dbus_wrapper_->IsMethodExported(kGetScreenBrightnessPercentMethod));
  EXPECT_FALSE(
      dbus_wrapper_->IsMethodExported(kIncreaseKeyboardBrightnessMethod));
  EXPECT_FALSE(
      dbus_wrapper_->IsMethodExported(kDecreaseKeyboardBrightnessMethod));

  // powerd shouldn't shut the system down in response to a low battery charge.
  system::PowerStatus status;
  status.battery_is_present = true;
  status.battery_below_shutdown_threshold = true;
  async_commands_.clear();
  power_supply_->set_status(status);
  power_supply_->NotifyObservers();
  EXPECT_EQ(0, async_commands_.size());
}

// TODO(chromeos-power): Add more tests. Namely:
// - PrepareToSuspend / UndoPrepareToSuspend
// - Creating and deleting suspend_announced file
// - Handling D-Bus RequestSuspend method calls
// - Reading wakeup_count
// - Fetching TPM counter status from cryptohome
// - Generating suspend IDs
// - Probably other stuff :-/

}  // namespace power_manager
