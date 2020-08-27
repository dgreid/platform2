// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "userspace_touchpad/uinputdevice.h"

UinputDevice::~UinputDevice() {
  // Tell the OD to destroy the uinput device as this object is destructed
  if (uinput_fd_ >= 0) {
    ioctl(uinput_fd_, UI_DEV_DESTROY);
  }
}

bool UinputDevice::CreateUinputFD() {
  // Open a control file descriptor for creating a new uinput device.
  // This file discriptor is used with ioctls to configure the device and
  // receive the outgoing event information.
  if (uinput_fd_ >= 0) {
    printf("Control FD already opened!  Aborting...\n");
    return false;
  }

  uinput_fd_ =
      syscall_handler_->open(UINPUT_CONTROL_FILENAME, O_WRONLY | O_NONBLOCK);
  if (uinput_fd_ < 0) {
    printf("Unable to open %s. (%d)\n", UINPUT_CONTROL_FILENAME, uinput_fd_);
    return false;
  }
  return true;
}

bool UinputDevice::EnableEventType(int ev_type) const {
  // Tell the kernel that this uinput device will report events of a
  // certain type (ABS, KEY, etc).  Individual event codes must still be
  // enabled individually, but their overarching types need to be enabled
  // first, which is done here.
  int error = syscall_handler_->ioctl(uinput_fd_, UI_SET_EVBIT, ev_type);
  if (error) {
    printf("Error: Unable to enable event type 0x%02X. (%d)\n", ev_type, error);
    return false;
  }
  printf("Enabled events of type 0x%02X.\n", ev_type);
  return true;
}

bool UinputDevice::EnableKeyEvent(int ev_code) const {
  // Tell the kernel that this region's uinput device will report a specific
  // key event. (eg: KEY_BACKSPACE or BTN_TOUCH)
  int error = syscall_handler_->ioctl(uinput_fd_, UI_SET_KEYBIT, ev_code);
  if (error) {
    printf("Error: Unable to enable EV_KEY 0x%02X events. (%d)\n", ev_code,
           error);
    return false;
  }
  printf("Enabled EV_KEY 0x%02X event.\n", ev_code);
  return true;
}

bool UinputDevice::EnableAbsEvent(int ev_code) const {
  // Tell the kernel that this region's uinput device will report a specific
  // kind of ABS event. (eg: ABS_MT_POSITION_X or ABS_PRESSURE)
  int error = syscall_handler_->ioctl(uinput_fd_, UI_SET_ABSBIT, ev_code);
  if (error) {
    printf("Error: Unable to enable EV_ABS 0x%02X events. (%d)\n", ev_code,
           error);
    return false;
  }
  printf("Enabled EV_ABS 0x%02X event.\n", ev_code);
  return true;
}

bool UinputDevice::CopyABSOutputEvents(int source_evdev_fd,
                                       int width,
                                       int height) const {
  // Configure this region's uinput device to report the correct kinds of
  // events by copying the events that are reported by the input device
  // who's file descriptor is passed as a reference.
  // Instead of copying the range of the absolute axes though, the user
  // specifies the width and height manually -- essentially creating a
  // cloned input device with a different size than the source device.
  int ev_code, error;
  struct uinput_abs_setup abs_setup;
  int64_t supported_abs_event_codes[NBITS(KEY_MAX)];
  int64_t supported_event_types[EV_MAX];

  // Query the source evdev filedescriptor to see which event types it
  // supports to make sure it supports ABS.
  memset(supported_event_types, 0, sizeof(supported_event_types));
  ioctl(source_evdev_fd, EVIOCGBIT(0, EV_MAX), supported_event_types);
  if (!test_bit(EV_ABS, supported_event_types)) {
    printf("ERROR: Touchscreen does not support EV_ABS events.\n");
    return false;
  }

  // Enable the EV_ABS event type for this device.  Fail if it can't
  if (!EnableEventType(EV_ABS)) {
    return false;
  }

  // Query the device to find which ABS event codes are supported and then
  // enable them for this uinput device as well.
  memset(supported_abs_event_codes, 0, sizeof(supported_abs_event_codes));
  ioctl(source_evdev_fd, EVIOCGBIT(EV_ABS, KEY_MAX), supported_abs_event_codes);
  for (ev_code = 0; ev_code < KEY_MAX; ev_code++) {
    // Skip over any event codes that're unsupported
    if (!test_bit(ev_code, supported_abs_event_codes)) {
      continue;
    }

    // Enable this event code for the uinput device
    if (!EnableAbsEvent(ev_code)) {
      return false;
    }

    // Fill in the ranges for each EV_ABS axis, modifying them for X and Y
    memset(&abs_setup, 0, sizeof(abs_setup));
    abs_setup.code = ev_code;
    ioctl(source_evdev_fd, EVIOCGABS(ev_code), &abs_setup.absinfo);
    if (ev_code == ABS_MT_POSITION_X || ev_code == ABS_X) {
      abs_setup.absinfo.minimum = 0;
      abs_setup.absinfo.maximum = width;
    } else if (ev_code == ABS_MT_POSITION_Y || ev_code == ABS_Y) {
      abs_setup.absinfo.minimum = 0;
      abs_setup.absinfo.maximum = height;
    }
    error = ioctl(uinput_fd_, UI_ABS_SETUP, &abs_setup);
    if (error) {
      printf("ERROR: Unable to setup axis for event code 0x%02x. (%d)\n",
             ev_code, error);
      return false;
    }
  }

  return true;
}

bool UinputDevice::FinalizeUinputCreation(
    std::string const& device_name) const {
  int error;
  struct uinput_setup device_info;

  // Build a uinput device struct and write it to the ui_fd to specify the
  // various identification parameters required such as the device name.
  memset(&device_info, 0, sizeof(device_info));
  snprintf(device_info.name, UINPUT_MAX_NAME_SIZE, "%s", device_name.c_str());
  device_info.id.bustype = BUS_USB;
  device_info.id.vendor = GOOGLE_VENDOR_ID;
  device_info.id.product = DUMMY_PRODUCT_ID;
  device_info.id.version = VERSION_NUMBER;
  error = syscall_handler_->ioctl(uinput_fd_, UI_DEV_SETUP, &device_info);
  if (error) {
    printf("Error on uinput device setup ioctl. (%d)\n", error);
    return false;
  }

  // Finally request that a new uinput device is created to those specs.
  // After this step the device should be fully functional and ready to
  // send events.
  error = syscall_handler_->ioctl(uinput_fd_, UI_DEV_CREATE);
  if (error) {
    printf("Error on uinput device creation ioctl. (%d)\n", error);
    return false;
  }
  return true;
}

bool UinputDevice::SendEvent(int ev_type, int ev_code, int value) const {
  // Send an input event to the kernel through this uinput device.
  struct input_event ev;
  ev.type = ev_type;
  ev.code = ev_code;
  ev.value = value;

  int bytes_written =
      syscall_handler_->write(uinput_fd_, &ev, sizeof(struct input_event));
  if (bytes_written != sizeof(struct input_event)) {
    printf("ERROR: Failed during write() when sending an event. (%d)\n",
           bytes_written);
    return false;
  }
  return true;
}
