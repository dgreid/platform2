// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/input_watcher.h"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>

#include <base/callback.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/common/prefs.h"
#include "power_manager/common/util.h"
#include "power_manager/powerd/system/acpi_wakeup_helper.h"
#include "power_manager/powerd/system/event_device_interface.h"
#include "power_manager/powerd/system/input_observer.h"
#include "power_manager/powerd/system/udev.h"

namespace power_manager {
namespace system {

namespace {

const char kSysClassInputPath[] = "/sys/class/input";
const char kDevInputPath[] = "/dev/input";
const char kInputBaseName[] = "event";

const char kInputMatchPattern[] = "input*";
const char kUsbMatchString[] = "usb";
const char kBluetoothMatchString[] = "bluetooth";

// Path to the console device where VT_GETSTATE ioctls are made to get the
// currently-active VT.
const char kConsolePath[] = "/dev/tty0";

// Given a string |name| consisting of kInputBaseName followed by a base-10
// integer, extracts the integer to |num_out|. Returns false if |name| didn't
// match the expected format.
bool GetInputNumber(const std::string& name, int* num_out) {
  if (!base::StartsWith(name, kInputBaseName, base::CompareCase::SENSITIVE))
    return false;
  size_t base_len = strlen(kInputBaseName);
  return base::StringToInt(name.substr(base_len, name.size() - base_len),
                           num_out);
}

// If |event| came from a lid switch, copies its state to |state_out| and
// returns true. Otherwise, leaves |state_out| untouched and returns false.
bool GetLidStateFromEvent(const input_event& event, LidState* state_out) {
  if (event.type != EV_SW || event.code != SW_LID)
    return false;

  *state_out = event.value == 1 ? LID_CLOSED : LID_OPEN;
  return true;
}

// If |event| came from a tablet mode switch, copies its state to |mode_out| and
// returns true. Otherwise, leaves |mode_out| untouched and returns false.
bool GetTabletModeFromEvent(const input_event& event,
                            TabletMode* mode_out) {
  if (event.type != EV_SW || event.code != SW_TABLET_MODE)
    return false;

  *mode_out = event.value == 1 ? TABLET_MODE_ON : TABLET_MODE_OFF;
  return true;
}

// If |event| came from a power button, copies its state to |state_out| and
// returns true. Otherwise, leaves |state_out| untouched and returns false.
bool GetPowerButtonStateFromEvent(const input_event& event,
                                  ButtonState* state_out) {
  if (event.type != EV_KEY || event.code != KEY_POWER)
    return false;

  switch (event.value) {
    case 0: *state_out = BUTTON_UP;      break;
    case 1: *state_out = BUTTON_DOWN;    break;
    case 2: *state_out = BUTTON_REPEAT;  break;
    default: LOG(ERROR) << "Unhandled button state " << event.value;
  }
  return true;
}

}  // namespace

const char InputWatcher::kInputUdevSubsystem[] = "input";
const char InputWatcher::kPowerButtonToSkip[] = "LNXPWRBN";
const char InputWatcher::kPowerButtonToSkipForLegacy[] = "isa";

InputWatcher::InputWatcher()
    : dev_input_path_(kDevInputPath),
      sys_class_input_path_(kSysClassInputPath),
      lid_device_(nullptr),
      tablet_mode_device_(nullptr),
      hover_device_(nullptr),
      use_lid_(true),
      lid_state_(LID_OPEN),
      tablet_mode_(TABLET_MODE_OFF),
      detect_hover_(false),
      hovering_(false),
      current_multitouch_slot_(0),
      multitouch_slots_hover_state_(0),
      single_touch_hover_valid_(false),
      single_touch_hover_distance_nonzero_(false),
      power_button_to_skip_(kPowerButtonToSkip),
      console_fd_(-1),
      udev_(nullptr),
      weak_ptr_factory_(this) {
}

InputWatcher::~InputWatcher() {
  if (udev_)
    udev_->RemoveSubsystemObserver(kInputUdevSubsystem, this);
  if (console_fd_ >= 0)
    close(console_fd_);
}

bool InputWatcher::Init(
    std::unique_ptr<EventDeviceFactoryInterface> event_device_factory,
    PrefsInterface* prefs,
    UdevInterface* udev) {
  event_device_factory_ = std::move(event_device_factory);
  udev_ = udev;

  prefs->GetBool(kUseLidPref, &use_lid_);
  if (!use_lid_)
    lid_state_ = LID_NOT_PRESENT;

  bool legacy_power_button = false;
  if (prefs->GetBool(kLegacyPowerButtonPref, &legacy_power_button) &&
      legacy_power_button)
    power_button_to_skip_ = kPowerButtonToSkipForLegacy;

  prefs->GetBool(kDetectHoverPref, &detect_hover_);

  udev_->AddSubsystemObserver(kInputUdevSubsystem, this);

  if (base::DirectoryExists(dev_input_path_)) {
    if (access(dev_input_path_.value().c_str(), R_OK|X_OK) != 0) {
      LOG(ERROR) << dev_input_path_.value() << " isn't readable";
      return false;
    }
    base::FileEnumerator enumerator(
        dev_input_path_, false, base::FileEnumerator::FILES);
    for (base::FilePath path = enumerator.Next(); !path.empty();
         path = enumerator.Next()) {
      const std::string name = path.BaseName().value();
      int num = -1;
      if (GetInputNumber(name, &num))
        HandleAddedInput(name, num);
    }
  }

  return true;
}

void InputWatcher::AddObserver(InputObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void InputWatcher::RemoveObserver(InputObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

LidState InputWatcher::QueryLidState() {
  if (!lid_device_)
    return LID_NOT_PRESENT;

  const uint32_t device_types = GetDeviceTypes(lid_device_);
  while (true) {
    // Stop when we fail to read any more events.
    std::vector<input_event> events;
    if (!lid_device_->ReadEvents(&events))
      break;

    // Get the state from the last lid event (|events| may also contain non-lid
    // events).
    for (std::vector<input_event>::const_reverse_iterator it =
             events.rbegin(); it != events.rend(); ++it) {
      if (GetLidStateFromEvent(*it, &lid_state_))
        break;
    }

    queued_events_.reserve(queued_events_.size() + events.size());
    for (auto event : events)
      queued_events_.push_back(std::make_pair(event, device_types));
    VLOG(1) << "Queued " << events.size()
            << " event(s) while querying lid state";
  }

  if (!queued_events_.empty()) {
    send_queued_events_task_.Reset(
        base::Bind(&InputWatcher::SendQueuedEvents, base::Unretained(this)));
    base::MessageLoop::current()->PostTask(
        FROM_HERE, send_queued_events_task_.callback());
  }

  return lid_state_;
}

TabletMode InputWatcher::GetTabletMode() {
  return tablet_mode_;
}

bool InputWatcher::IsUSBInputDeviceConnected() const {
  base::FileEnumerator enumerator(sys_class_input_path_, false,
      static_cast<::base::FileEnumerator::FileType>(
          base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS),
      kInputMatchPattern);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::FilePath symlink_path;
    if (!base::ReadSymbolicLink(path, &symlink_path))
      continue;
    const std::string& path_string = symlink_path.value();
    // Skip bluetooth devices, which may be identified as USB devices.
    if (path_string.find(kBluetoothMatchString) != std::string::npos)
      continue;
    // Now look for the USB devices that are not bluetooth.
    size_t position = path_string.find(kUsbMatchString);
    if (position == std::string::npos)
      continue;
    // Now that the string "usb" has been found, make sure it is a whole word
    // and not just part of another word like "busbreaker".
    bool usb_at_word_head =
        position == 0 || !base::IsAsciiAlpha(path_string.at(position - 1));
    bool usb_at_word_tail =
        position + strlen(kUsbMatchString) == path_string.size() ||
        !base::IsAsciiAlpha(path_string.at(position + strlen(kUsbMatchString)));
    if (usb_at_word_head && usb_at_word_tail)
      return true;
  }
  return false;
}

int InputWatcher::GetActiveVT() {
  // It's not worthwhile creating an interface around this single ioctl to query
  // the active VT. Defer opening the console until this method is called so
  // that unit tests can step around this code.
  if (console_fd_ < 0) {
    if ((console_fd_ = open(kConsolePath, O_WRONLY)) == -1) {
      PLOG(ERROR) << "Unable to open " << kConsolePath;
      return -1;
    }
  }
  struct vt_stat state;
  if (ioctl(console_fd_, VT_GETSTATE, &state) == -1) {
    PLOG(ERROR) << "VT_GETSTATE ioctl on " << kConsolePath << "failed";
    return -1;
  }
  return state.v_active;
}

void InputWatcher::OnUdevEvent(const std::string& subsystem,
                               const std::string& sysname,
                               UdevAction action) {
  DCHECK_EQ(subsystem, kInputUdevSubsystem);
  int input_num = -1;
  if (GetInputNumber(sysname, &input_num)) {
    if (action == UDEV_ACTION_ADD)
      HandleAddedInput(sysname, input_num);
    else if (action == UDEV_ACTION_REMOVE)
      HandleRemovedInput(input_num);
  }
}

uint32_t InputWatcher::GetDeviceTypes(
    const EventDeviceInterface* device) const {
  uint32_t device_types = DEVICE_NONE;
  if (power_button_devices_.count(device))
    device_types |= DEVICE_POWER_BUTTON;
  if (device == lid_device_)
    device_types |= DEVICE_LID_SWITCH;
  if (device == tablet_mode_device_)
    device_types |= DEVICE_TABLET_MODE_SWITCH;
  if (device == hover_device_)
    device_types |= DEVICE_HOVER;
  return device_types;
}

void InputWatcher::OnNewEvents(EventDeviceInterface* device) {
  SendQueuedEvents();

  std::vector<input_event> events;
  if (!device->ReadEvents(&events))
    return;

  VLOG(1) << "Read " << events.size() << " event(s) from "
          << device->GetDebugName();
  const uint32_t device_types = GetDeviceTypes(device);
  for (size_t i = 0; i < events.size(); ++i) {
    // Update |lid_state_| here instead of in ProcessEvent() so we can avoid
    // modifying it in response to queued events.
    if (device_types & DEVICE_LID_SWITCH)
      GetLidStateFromEvent(events[i], &lid_state_);
    ProcessEvent(events[i], device_types);
  }
}

void InputWatcher::ProcessEvent(const input_event& event,
                                uint32_t device_types) {
  LidState lid_state = LID_OPEN;
  if ((device_types & DEVICE_LID_SWITCH) &&
      GetLidStateFromEvent(event, &lid_state)) {
    VLOG(1) << "Notifying observers about lid " << LidStateToString(lid_state)
            << " event";
    FOR_EACH_OBSERVER(InputObserver, observers_, OnLidEvent(lid_state));
  }

  TabletMode tablet_mode = TABLET_MODE_OFF;
  if (device_types & DEVICE_TABLET_MODE_SWITCH &&
      GetTabletModeFromEvent(event, &tablet_mode)) {
    tablet_mode_ = tablet_mode;
    VLOG(1) << "Notifying observers about tablet mode "
            << TabletModeToString(tablet_mode) << " event";
    FOR_EACH_OBSERVER(InputObserver, observers_,
                      OnTabletModeEvent(tablet_mode));
  }

  ButtonState button_state = BUTTON_DOWN;
  if ((device_types & DEVICE_POWER_BUTTON) &&
      GetPowerButtonStateFromEvent(event, &button_state)) {
    VLOG(1) << "Notifying observers about power button "
            << ButtonStateToString(button_state) << " event";
    FOR_EACH_OBSERVER(InputObserver, observers_,
                      OnPowerButtonEvent(button_state));
  }

  if (device_types & DEVICE_HOVER)
    ProcessHoverEvent(event);
}

void InputWatcher::ProcessHoverEvent(const input_event& event) {
  if (event.type == EV_ABS && event.code == ABS_MT_SLOT) {
    VLOG(2) << "ABS_MT_SLOT " << event.value;
    // ABS_MT_SLOT events announce the slot that following multitouch events
    // will refer to.
    if (event.value < 0 ||
        event.value >=
        static_cast<int>(sizeof(multitouch_slots_hover_state_) * 8)) {
      LOG(WARNING) << "Ignoring ABS_MT_SLOT event for slot "
                   << event.value;
      current_multitouch_slot_ = -1;
    } else {
      current_multitouch_slot_ = event.value;
    }
  } else if (event.type == EV_ABS && event.code == ABS_MT_TRACKING_ID) {
    VLOG(2) << "ABS_MT_TRACKING_ID " << event.value;
    // ABS_MT_TRACKING_ID events associate a tracking ID with the current slot,
    // with -1 indicating that the slot is unused. Use them as a proxy for
    // whether the slot is reporting a hover (or touch).
    if (current_multitouch_slot_ >= 0) {
      const uint64_t slot_bit =
          static_cast<uint64_t>(1) << current_multitouch_slot_;
      if (event.value >= 0)
        multitouch_slots_hover_state_ |= slot_bit;
      else
        multitouch_slots_hover_state_ &= ~slot_bit;
    }
  } else if (event.type == EV_ABS && event.code == ABS_DISTANCE) {
    // For single-touch presence-only hover touchpads, ABS_DISTANCE indicates
    // the distance above the pad the single-touch finger is hovering
    VLOG(2) << "ABS_DISTANCE " << event.value;
    single_touch_hover_distance_nonzero_ = (event.value > 0);
  } else if (event.type == EV_KEY && event.code == BTN_TOOL_FINGER) {
    // For single-touch presence-only hover touchpads, BTN_TOOL_FINGER tells
    // us if the single-touch contact is valid (if we should believe the
    // value in ABS_DISTANCE)
    VLOG(2) << "BTN_TOOL_FINGER " << event.value;
    single_touch_hover_valid_ = (event.value == 1);
  } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
    // SYN_REPORT events indicate the end of the current set of multitouch data.
    // Check whether the overall hovering state is different from before and
    // notify observers if so.
    VLOG(2) << "SYN_REPORT";
    bool multi_touch_hovering = multitouch_slots_hover_state_ != 0;
    bool single_touch_hovering = (single_touch_hover_distance_nonzero_ &&
                                  single_touch_hover_valid_);
    bool hovering = multi_touch_hovering || single_touch_hovering;
    if (hovering != hovering_) {
      VLOG(1) << "Notifying observers about hover state change to "
              << (hovering ? "on" : "off");
      hovering_ = hovering;
      FOR_EACH_OBSERVER(InputObserver, observers_,
                        OnHoverStateChanged(hovering_));
    }
  }
}

void InputWatcher::HandleAddedInput(const std::string& input_name,
                                    int input_num) {
  if (event_devices_.count(input_num) > 0) {
    LOG(WARNING) << "Input " << input_num << " already registered";
    return;
  }

  const base::FilePath path = dev_input_path_.Append(input_name);
  linked_ptr<EventDeviceInterface> device(event_device_factory_->Open(path));
  if (!device.get()) {
    LOG(ERROR) << "Failed to open " << path.value();
    return;
  }

  const std::string phys = device->GetPhysPath();
  if (base::StartsWith(phys, power_button_to_skip_,
                       base::CompareCase::SENSITIVE)) {
    VLOG(1) << "Skipping event device with phys path: " << phys;
    return;
  }

  bool should_watch = false;
  if (device->IsPowerButton()) {
    LOG(INFO) << "Watching power button: " << device->GetDebugName();
    should_watch = true;
    power_button_devices_.insert(device.get());
  }

  // Note that it's possible for a power button and lid switch to share a single
  // event device.
  if (use_lid_ && device->IsLidSwitch()) {
    if (lid_device_) {
      LOG(WARNING) << "Skipping additional lid switch device "
                   << device->GetDebugName();
    } else {
      LOG(INFO) << "Watching lid switch: " << device->GetDebugName();
      should_watch = true;
      lid_device_ = device.get();
      lid_state_ = device->GetInitialLidState();
      VLOG(1) << "Initial lid state is " << LidStateToString(lid_state_);
    }
  }

  if (device->IsTabletModeSwitch()) {
    if (tablet_mode_device_) {
      LOG(WARNING) << "Skipping additional tablet mode switch "
                   << device->GetDebugName();
    } else {
      LOG(INFO) << "Watching tablet mode switch: " << device->GetDebugName();
      should_watch = true;
      tablet_mode_device_ = device.get();
      tablet_mode_ = device->GetInitialTabletMode();
      VLOG(1) << "Initial tablet mode state is "
              << TabletModeToString(tablet_mode_);
    }
  }

  if (detect_hover_ && device->HoverSupported() && device->HasLeftButton()) {
    if (hover_device_) {
      LOG(WARNING) << "Skipping additional hover device "
                   << device->GetDebugName();
    } else {
      LOG(INFO) << "Watching hover device: " << device->GetDebugName();
      should_watch = true;
      hover_device_ = device.get();
    }
  }

  if (should_watch) {
    device->WatchForEvents(base::Bind(&InputWatcher::OnNewEvents,
                                      weak_ptr_factory_.GetWeakPtr(),
                                      base::Unretained(device.get())));
    event_devices_.insert(std::make_pair(input_num, device));
  }
}


void InputWatcher::HandleRemovedInput(int input_num) {
  InputMap::iterator it = event_devices_.find(input_num);
  if (it == event_devices_.end())
    return;
  LOG(INFO) << "Stopping watching " << it->second->GetDebugName();
  power_button_devices_.erase(it->second.get());
  if (lid_device_ == it->second.get())
    lid_device_ = nullptr;
  if (tablet_mode_device_ == it->second.get())
    tablet_mode_device_ = nullptr;
  if (hover_device_ == it->second.get())
    hover_device_ = nullptr;
  event_devices_.erase(it);
}

void InputWatcher::SendQueuedEvents() {
  for (auto event_pair : queued_events_)
    ProcessEvent(event_pair.first, event_pair.second);
  queued_events_.clear();
}

}  // namespace system
}  // namespace power_manager
