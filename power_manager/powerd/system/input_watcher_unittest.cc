// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/input_watcher.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <gtest/gtest.h>
#include <linux/input.h>

#include "power_manager/common/action_recorder.h"
#include "power_manager/common/fake_prefs.h"
#include "power_manager/powerd/system/event_device_stub.h"
#include "power_manager/powerd/system/input_observer.h"
#include "power_manager/powerd/system/udev_stub.h"

namespace power_manager {
namespace system {
namespace {

// Strings that can be compared against TestObserver::GetActions().
const char kLidClosedAction[] = "lid-closed";
const char kLidOpenAction[] = "lid-open";
const char kTabletModeOnAction[] = "tablet-mode-on";
const char kTabletModeOffAction[] = "tablet-mode-off";
const char kPowerButtonDownAction[] = "power-down";
const char kPowerButtonUpAction[] = "power-down";
const char kHoverOnAction[] = "hover-on";
const char kHoverOffAction[] = "hover-off";
const char kNoActions[] = "";

}  // namespace

// InputObserver implementation that just records the events that it receives.
class TestObserver : public InputObserver,
                     public ActionRecorder {
 public:
  explicit TestObserver(InputWatcher* watcher)
      : watcher_(watcher) {
    watcher_->AddObserver(this);
  }
  ~TestObserver() override {
    watcher_->RemoveObserver(this);
  }

  // InputObserver implementation:
  void OnLidEvent(LidState state) override {
    CHECK(state == LID_OPEN || state == LID_CLOSED);
    AppendAction(state == LID_OPEN ? kLidOpenAction : kLidClosedAction);
  }
  void OnTabletModeEvent(TabletMode mode) override {
    CHECK(mode == TABLET_MODE_ON || mode == TABLET_MODE_OFF);
    AppendAction(mode == TABLET_MODE_ON ? kTabletModeOnAction :
                 kTabletModeOffAction);
  }
  void OnPowerButtonEvent(ButtonState state) override {
    CHECK(state == BUTTON_DOWN || state == BUTTON_UP);
    AppendAction(state == BUTTON_DOWN ? kPowerButtonDownAction :
                 kPowerButtonUpAction);
  }
  void OnHoverStateChanged(bool hovering) override {
    AppendAction(hovering ? kHoverOnAction : kHoverOffAction);
  }

 private:
  InputWatcher* watcher_;  // Not owned.

  DISALLOW_COPY_AND_ASSIGN(TestObserver);
};

class InputWatcherTest : public testing::Test {
 public:
  InputWatcherTest()
      : scoped_device_factory_(new EventDeviceFactoryStub()),
        device_factory_(scoped_device_factory_.get()),
        use_lid_pref_(1),
        legacy_power_button_pref_(0),
        detect_hover_pref_(0) {
    CHECK(temp_dir_.CreateUniqueTempDir());

    dev_input_path_ = temp_dir_.path().Append(base::FilePath("dev/input"));
    CHECK(base::CreateDirectory(dev_input_path_));

    sys_class_input_path_ =
        temp_dir_.path().Append(base::FilePath("sys/class/input"));
    CHECK(base::CreateDirectory(sys_class_input_path_));
  }
  ~InputWatcherTest() override {}

 protected:
  // Initializes |input_watcher_|. Intended to be called by tests after
  // initializing |device_factory_|. Safe to be called multiple times.
  void Init() {
    prefs_.SetInt64(kUseLidPref, use_lid_pref_);
    prefs_.SetInt64(kLegacyPowerButtonPref, legacy_power_button_pref_);
    prefs_.SetInt64(kDetectHoverPref, detect_hover_pref_);

    // If Init() has already been called, reuse the existing factory.
    if (input_watcher_) {
      ASSERT_EQ(device_factory_,
                input_watcher_->release_event_device_factory_for_testing());
      scoped_device_factory_.reset(device_factory_);
      observer_.reset();
    }
    input_watcher_.reset(new InputWatcher);
    input_watcher_->set_dev_input_path_for_testing(dev_input_path_);
    input_watcher_->set_sys_class_input_path_for_testing(sys_class_input_path_);
    ASSERT_TRUE(input_watcher_->Init(
        std::move(scoped_device_factory_), &prefs_, &udev_));

    observer_.reset(new TestObserver(input_watcher_.get()));
  }

  // Registers |device| named |name| within |dev_input_dir_|. To be recognized,
  // |name| should be of the form "event<num>".
  void AddDevice(const std::string& name,
                 linked_ptr<EventDeviceStub> device) {
    const base::FilePath path = dev_input_path_.Append(name);
    ASSERT_EQ(0, base::WriteFile(path, "", 0));
    device_factory_->RegisterDevice(path, device);
  }

  FakePrefs prefs_;
  base::ScopedTempDir temp_dir_;

  // Temp dir used in place of /dev/input.
  base::FilePath dev_input_path_;

  // Temp dir used in place of /sys/class/input.
  base::FilePath sys_class_input_path_;

  // Passed to |input_watcher_| in Init(); use |device_factory_| instead.
  std::unique_ptr<EventDeviceFactoryStub> scoped_device_factory_;

  UdevStub udev_;
  EventDeviceFactoryStub* device_factory_;  // Owned by |input_watcher_|.
  std::unique_ptr<InputWatcher> input_watcher_;
  std::unique_ptr<TestObserver> observer_;

  // Initial values for prefs.
  int64_t use_lid_pref_;
  int64_t legacy_power_button_pref_;
  int64_t detect_hover_pref_;
};

TEST_F(InputWatcherTest, DetectUSBDevices) {
  // Test the detector on empty directory.
  Init();
  EXPECT_FALSE(input_watcher_->IsUSBInputDeviceConnected());

  // Create a bunch of non-USB paths.
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../foo0/dev:1/00:00"),
                  sys_class_input_path_.Append("input0")));
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../bar4/dev:2/00:00"),
                  sys_class_input_path_.Append("input1")));
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../goo3/dev:3/00:00"),
                  sys_class_input_path_.Append("input2")));
  EXPECT_FALSE(input_watcher_->IsUSBInputDeviceConnected());

  // Create a "fake USB" path that contains "usb" as part of another word
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../busbreaker/00:00"),
                  sys_class_input_path_.Append("input3")));
  EXPECT_FALSE(input_watcher_->IsUSBInputDeviceConnected());

  // Create a true USB path.
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../usb3/dev:3/00:00"),
                  sys_class_input_path_.Append("input4")));
  EXPECT_TRUE(input_watcher_->IsUSBInputDeviceConnected());

  // Clear directory and create a USB path.
  ASSERT_TRUE(base::DeleteFile(sys_class_input_path_, true));
  ASSERT_TRUE(base::CreateDirectory(sys_class_input_path_));
  ASSERT_TRUE(base::CreateSymbolicLink(
                  sys_class_input_path_.Append("../../usb/dev:5/00:00"),
                  sys_class_input_path_.Append("input10")));
  EXPECT_TRUE(input_watcher_->IsUSBInputDeviceConnected());

  // Clear directory and create a non-symlink USB path. It should not counted
  // because all the input paths should be symlinks.
  ASSERT_TRUE(base::DeleteFile(sys_class_input_path_, true));
  ASSERT_TRUE(base::CreateDirectory(sys_class_input_path_));
  ASSERT_TRUE(base::CreateDirectory(sys_class_input_path_.Append("usb12")));
  EXPECT_FALSE(input_watcher_->IsUSBInputDeviceConnected());
}

TEST_F(InputWatcherTest, PowerButton) {
  // Create an ACPI power button device that should be skipped.
  linked_ptr<EventDeviceStub> skipped_power_button(new EventDeviceStub);
  skipped_power_button->set_phys_path(
      std::string(InputWatcher::kPowerButtonToSkip) + "0");
  skipped_power_button->set_is_power_button(true);
  AddDevice("event0", skipped_power_button);

  linked_ptr<EventDeviceStub> power_button(new EventDeviceStub);
  power_button->set_is_power_button(true);
  AddDevice("event1", power_button);
  Init();

  power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  power_button->AppendEvent(EV_KEY, KEY_POWER, 0);
  power_button->AppendEvent(EV_KEY, KEY_VOLUMEDOWN, 1);
  power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  power_button->NotifyAboutEvents();
  EXPECT_EQ(JoinActions(kPowerButtonDownAction,
                        kPowerButtonUpAction,
                        kPowerButtonDownAction,
                        NULL),
            observer_->GetActions());

  // Check that an event from the ACPI button isn't reported.
  skipped_power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  skipped_power_button->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // Now set the legacy power button pref and test again.
  legacy_power_button_pref_ = 1;
  skipped_power_button->set_phys_path(
      std::string(InputWatcher::kPowerButtonToSkipForLegacy) + "0");
  Init();

  power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  power_button->NotifyAboutEvents();
  EXPECT_EQ(kPowerButtonDownAction, observer_->GetActions());

  skipped_power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  skipped_power_button->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());
}

TEST_F(InputWatcherTest, LidSwitch) {
  linked_ptr<EventDeviceStub> lid_switch(new EventDeviceStub);
  lid_switch->set_is_lid_switch(true);
  lid_switch->set_initial_lid_state(LID_CLOSED);
  AddDevice("event0", lid_switch);

  // Before any events have been received, check that the initially-read state
  // is returned.
  Init();
  EXPECT_EQ(LID_CLOSED, input_watcher_->QueryLidState());

  // Add an event and requery. The updated state should be returned but the
  // action shouldn't yet be sent to the observer.
  lid_switch->AppendEvent(EV_SW, SW_LID, 0);
  EXPECT_EQ(LID_OPEN, input_watcher_->QueryLidState());
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // When the message loop runs, the event should be sent.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kLidOpenAction, observer_->GetActions());

  // Query again, but this time add some more events and notify about them
  // before running the message loop. Both the queued and new events should be
  // sent.
  lid_switch->AppendEvent(EV_SW, SW_LID, 1);
  EXPECT_EQ(LID_CLOSED, input_watcher_->QueryLidState());
  lid_switch->AppendEvent(EV_SW, SW_LID, 0);
  lid_switch->AppendEvent(EV_SW, SW_LID, 1);
  lid_switch->NotifyAboutEvents();
  EXPECT_EQ(JoinActions(kLidClosedAction,
                        kLidOpenAction,
                        kLidClosedAction,
                        NULL),
            observer_->GetActions());

  // There aren't any more events to send.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // Set a pref saying there's no lid and check that the queried state is
  // reported correctly.
  use_lid_pref_ = 0;
  Init();
  EXPECT_EQ(LID_NOT_PRESENT, input_watcher_->QueryLidState());

  // The switch shouldn't be watched for events either.
  lid_switch->AppendEvent(EV_SW, SW_LID, 1);
  lid_switch->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());
}

TEST_F(InputWatcherTest, TabletModeSwitch) {
  linked_ptr<EventDeviceStub> tablet_mode_switch(new EventDeviceStub);
  tablet_mode_switch->set_is_tablet_mode_switch(true);
  tablet_mode_switch->set_initial_tablet_mode(TABLET_MODE_ON);
  AddDevice("event0", tablet_mode_switch);

  // Before any events have been received, check that the initially-read mode is
  // returned.
  Init();
  EXPECT_EQ(TABLET_MODE_ON, input_watcher_->GetTabletMode());

  // Add an event, run the message loop, and check that the observer was
  // notified and that GetTabletMode() returns the updated mode.
  tablet_mode_switch->AppendEvent(EV_SW, SW_TABLET_MODE, 0);
  tablet_mode_switch->NotifyAboutEvents();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kTabletModeOffAction, observer_->GetActions());
  EXPECT_EQ(TABLET_MODE_OFF, input_watcher_->GetTabletMode());

  // Now enable tablet mode.
  tablet_mode_switch->AppendEvent(EV_SW, SW_TABLET_MODE, 1);
  tablet_mode_switch->NotifyAboutEvents();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(kTabletModeOnAction, observer_->GetActions());
  EXPECT_EQ(TABLET_MODE_ON, input_watcher_->GetTabletMode());
}

TEST_F(InputWatcherTest, HoverMultitouch) {
  linked_ptr<EventDeviceStub> touchpad(new EventDeviceStub);
  touchpad->set_debug_name("touchpad");
  touchpad->set_hover_supported(true);
  touchpad->set_has_left_button(true);
  AddDevice("event0", touchpad);

  linked_ptr<EventDeviceStub> touchscreen(new EventDeviceStub);
  touchscreen->set_debug_name("touchscreen");
  touchscreen->set_hover_supported(true);
  touchscreen->set_has_left_button(false);
  AddDevice("event1", touchscreen);

  detect_hover_pref_ = 1;
  Init();

  // Indicate that a finger is being tracked. No events should be generated yet.
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // After a SYN_REPORT indicating the end of the current report, hovering
  // should be reported.
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOnAction, observer_->GetActions());

  // Detach the tracking ID, which should result in hovering stopping.
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOffAction, observer_->GetActions());

  // Report that two fingers are being tracked.
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, 1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 2);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOnAction, observer_->GetActions());

  // Stop tracking the first finger.
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, 0);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // Stop tracking the second finger, which should cause the cessation of
  // hovering.
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, 1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOffAction, observer_->GetActions());

  // Ignore reports about negative or highly-numbered slots.
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, -1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 3);
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, 64);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 4);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // The touchpad should be ignored when the pref is set to false.
  detect_hover_pref_ = 0;
  Init();
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 0);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());
}

TEST_F(InputWatcherTest, HoverSingletouch) {
  linked_ptr<EventDeviceStub> touchpad(new EventDeviceStub);
  touchpad->set_debug_name("touchpad");
  touchpad->set_hover_supported(true);
  touchpad->set_has_left_button(true);
  AddDevice("event0", touchpad);

  linked_ptr<EventDeviceStub> touchscreen(new EventDeviceStub);
  touchscreen->set_debug_name("touchscreen");
  touchscreen->set_hover_supported(true);
  touchscreen->set_has_left_button(false);
  AddDevice("event1", touchscreen);

  detect_hover_pref_ = 1;
  Init();

  // Show a finger approaching the pad, but not touching it yet.
  touchpad->AppendEvent(EV_ABS, ABS_DISTANCE, 1);
  touchpad->AppendEvent(EV_KEY, BTN_TOOL_FINGER, 1);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // After a SYN_REPORT indicating the end of the current report, hovering
  // should be reported.
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOnAction, observer_->GetActions());

  // Invalidate the hovering finger (leaving without touching the pad at
  // all) which should turn hover off.
  touchpad->AppendEvent(EV_KEY, BTN_TOOL_FINGER, 0);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOffAction, observer_->GetActions());

  // Report The finger coming back, but actually touching this time.
  touchpad->AppendEvent(EV_ABS, ABS_MT_SLOT, 1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 1);
  touchpad->AppendEvent(EV_ABS, ABS_MT_POSITION_X, 100);
  touchpad->AppendEvent(EV_ABS, ABS_MT_POSITION_Y, 100);
  touchpad->AppendEvent(EV_ABS, ABS_MT_PRESSURE, 50);
  touchpad->AppendEvent(EV_KEY, BTN_TOOL_FINGER, 1);
  touchpad->AppendEvent(EV_KEY, BTN_TOUCH, 1);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->AppendEvent(EV_ABS, ABS_DISTANCE, 0);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOnAction, observer_->GetActions());

  // Finger leaving the pad now.
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
  touchpad->AppendEvent(EV_KEY, BTN_TOOL_FINGER, 0);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kHoverOffAction, observer_->GetActions());

  // The touchpad should be ignored when the pref is set to false.
  detect_hover_pref_ = 0;
  Init();
  touchpad->AppendEvent(EV_ABS, ABS_DISTANCE, 1);
  touchpad->AppendEvent(EV_KEY, BTN_TOOL_FINGER, 1);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());
}

TEST_F(InputWatcherTest, IgnoreDevices) {
  // Create a device that looks like a power button but that doesn't follow the
  // expected device naming scheme. InputWatcher shouldn't request eents from
  // it.
  linked_ptr<EventDeviceStub> other_device(new EventDeviceStub);
  other_device->set_is_power_button(true);
  AddDevice("foo1", other_device);

  // Create a touchpad that doesn't support hover and check that it's also
  // ignored.
  detect_hover_pref_ = 1;
  linked_ptr<EventDeviceStub> touchpad(new EventDeviceStub);
  touchpad->set_has_left_button(true);
  touchpad->set_hover_supported(false);
  AddDevice("event0", touchpad);

  Init();
  EXPECT_TRUE(other_device->new_events_cb().is_null());
  EXPECT_TRUE(touchpad->new_events_cb().is_null());
}

TEST_F(InputWatcherTest, IgnoreUnexpectedEvents) {
  linked_ptr<EventDeviceStub> touchpad(new EventDeviceStub);
  touchpad->set_debug_name("touchpad");
  touchpad->set_hover_supported(true);
  touchpad->set_has_left_button(true);
  AddDevice("event0", touchpad);

  linked_ptr<EventDeviceStub> power_button(new EventDeviceStub);
  power_button->set_debug_name("power_button");
  power_button->set_is_power_button(true);
  AddDevice("event1", power_button);

  linked_ptr<EventDeviceStub> lid_switch(new EventDeviceStub);
  lid_switch->set_debug_name("lid_switch");
  lid_switch->set_is_lid_switch(true);
  lid_switch->set_initial_lid_state(LID_OPEN);
  AddDevice("event2", lid_switch);

  linked_ptr<EventDeviceStub> tablet_mode_switch(new EventDeviceStub);
  tablet_mode_switch->set_debug_name("tablet_mode_switch");
  tablet_mode_switch->set_is_tablet_mode_switch(true);
  tablet_mode_switch->set_initial_tablet_mode(TABLET_MODE_ON);
  AddDevice("event3", tablet_mode_switch);

  detect_hover_pref_ = 1;
  Init();

  // Bizarro world: touchpad reports power-button-down, power button reports
  // lid-closed, lid switch reports tablet-mode-on, tablet mode switch reports
  // hover. All of these events should be ignored.
  touchpad->AppendEvent(EV_KEY, KEY_POWER, 1);
  touchpad->NotifyAboutEvents();
  power_button->AppendEvent(EV_SW, SW_LID, 1);
  power_button->NotifyAboutEvents();
  lid_switch->AppendEvent(EV_SW, SW_TABLET_MODE, 1);
  lid_switch->NotifyAboutEvents();
  tablet_mode_switch->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 0);
  tablet_mode_switch->AppendEvent(EV_SYN, SYN_REPORT, 0);
  tablet_mode_switch->NotifyAboutEvents();
  power_button->NotifyAboutEvents();
  EXPECT_EQ(kNoActions, observer_->GetActions());

  // Now send events from the correct devices and check that they're reported to
  // the observer.
  touchpad->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 0);
  touchpad->AppendEvent(EV_SYN, SYN_REPORT, 0);
  touchpad->NotifyAboutEvents();
  power_button->AppendEvent(EV_KEY, KEY_POWER, 1);
  power_button->NotifyAboutEvents();
  lid_switch->AppendEvent(EV_SW, SW_LID, 1);
  lid_switch->NotifyAboutEvents();
  tablet_mode_switch->AppendEvent(EV_SW, SW_TABLET_MODE, 1);
  tablet_mode_switch->NotifyAboutEvents();
  EXPECT_EQ(JoinActions(kHoverOnAction,
                        kPowerButtonDownAction,
                        kLidClosedAction,
                        kTabletModeOnAction,
                        NULL),
            observer_->GetActions());
}

TEST_F(InputWatcherTest, SingleDeviceForAllTypes) {
  // Just to make sure that overlap is handled correctly, create a single device
  // that claims to report all types of events.
  linked_ptr<EventDeviceStub> device(new EventDeviceStub);
  device->set_hover_supported(true);
  device->set_has_left_button(true);
  device->set_is_power_button(true);
  device->set_is_lid_switch(true);
  device->set_initial_lid_state(LID_OPEN);
  device->set_is_tablet_mode_switch(true);
  device->set_initial_tablet_mode(TABLET_MODE_OFF);
  AddDevice("event0", device);
  detect_hover_pref_ = 1;
  Init();

  device->AppendEvent(EV_ABS, ABS_MT_TRACKING_ID, 0);
  device->AppendEvent(EV_KEY, KEY_POWER, 1);
  device->AppendEvent(EV_SW, SW_LID, 1);
  device->AppendEvent(EV_SW, SW_TABLET_MODE, 1);
  device->AppendEvent(EV_SYN, SYN_REPORT, 0);
  device->NotifyAboutEvents();
  EXPECT_EQ(JoinActions(kPowerButtonDownAction,
                        kLidClosedAction,
                        kTabletModeOnAction,
                        kHoverOnAction,
                        NULL),
            observer_->GetActions());
}

TEST_F(InputWatcherTest, RegisterForUdevEvents) {
  Init();
  EXPECT_TRUE(udev_.HasSubsystemObserver(InputWatcher::kInputUdevSubsystem,
                                         input_watcher_.get()));

  // Connect a keyboard and send a power button event.
  const char kDeviceName[] = "event0";
  linked_ptr<EventDeviceStub> keyboard(new EventDeviceStub);
  keyboard->set_is_power_button(true);
  AddDevice(kDeviceName, keyboard);
  input_watcher_->OnUdevEvent(InputWatcher::kInputUdevSubsystem,
                              kDeviceName, UDEV_ACTION_ADD);
  keyboard->AppendEvent(EV_KEY, KEY_POWER, 1);
  keyboard->NotifyAboutEvents();
  EXPECT_EQ(kPowerButtonDownAction, observer_->GetActions());

  // Disconnect the keyboard.
  input_watcher_->OnUdevEvent(InputWatcher::kInputUdevSubsystem,
                              kDeviceName, UDEV_ACTION_REMOVE);

  // Check that the InputWatcher unregisters itself.
  InputWatcher* dead_ptr = input_watcher_.get();
  observer_.reset();
  input_watcher_.reset();
  EXPECT_FALSE(udev_.HasSubsystemObserver(InputWatcher::kInputUdevSubsystem,
                                          dead_ptr));
}

TEST_F(InputWatcherTest, TolerateMissingDevInputDirectory) {
  // /dev/input may not exist on systems that lack lid switches or power
  // buttons: http://b/29239109. Check that InputWatcher doesn't report failure
  // in this case.
  use_lid_pref_ = 0;
  dev_input_path_ = base::FilePath("nonexistent/path");
  Init();
  EXPECT_EQ(LID_NOT_PRESENT, input_watcher_->QueryLidState());
  EXPECT_EQ(TABLET_MODE_OFF, input_watcher_->GetTabletMode());
  EXPECT_FALSE(input_watcher_->IsUSBInputDeviceConnected());
}

TEST_F(InputWatcherTest, DevInputDirectoryMustBeReadable) {
  // Init() should report failure if /dev/input exists but isn't readable.
  ASSERT_TRUE(base::SetPosixFilePermissions(dev_input_path_, 0000));
  InputWatcher input_watcher;
  input_watcher.set_dev_input_path_for_testing(dev_input_path_);
  input_watcher.set_sys_class_input_path_for_testing(sys_class_input_path_);
  EXPECT_FALSE(input_watcher.Init(
      std::move(scoped_device_factory_), &prefs_, &udev_));
}

}  // namespace system
}  // namespace power_manager
