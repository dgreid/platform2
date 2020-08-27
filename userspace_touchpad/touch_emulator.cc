// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We want to use assert in production environment
#undef NDEBUG

#include <cassert>
#include <cstring>

#include "userspace_touchpad/touch_emulator.h"

// Touchpad size and resolution.
constexpr int kXMax = 1920;  // points
constexpr int kYMax = 1080;  // points
constexpr int kXRes = 16;    // points/mm
constexpr int kYRes = 16;    // points/mm

TouchEmulator::TouchEmulator() : touch_device_handler_(nullptr) {
  CreateVirtualMultiTouchDevice();

  // Clear slot usage.
  for (size_t i = 0; i < kMaxFingers; ++i)
    slot_tid_[i] = -1;
}

void TouchEmulator::AbsInit(int code, int min_val, int max_val, int res) {
  struct uinput_abs_setup abs_setup = {0};
  assert(touch_device_handler_.EnableAbsEvent(code));
  abs_setup.code = code;
  abs_setup.absinfo.minimum = min_val;
  abs_setup.absinfo.maximum = max_val;
  abs_setup.absinfo.resolution = res;
  assert(ioctl(touch_device_handler_.get_fd(), UI_ABS_SETUP, &abs_setup) == 0);
}

void TouchEmulator::CreateVirtualMultiTouchDevice() {
  // Setup virtual touchpad.
  assert(touch_device_handler_.CreateUinputFD());

  assert(touch_device_handler_.EnableEventType(EV_KEY));
  assert(touch_device_handler_.EnableKeyEvent(BTN_LEFT));
  assert(touch_device_handler_.EnableKeyEvent(BTN_TOOL_FINGER));
  assert(touch_device_handler_.EnableKeyEvent(BTN_TOUCH));
  assert(touch_device_handler_.EnableKeyEvent(BTN_TOOL_DOUBLETAP));
  assert(touch_device_handler_.EnableKeyEvent(BTN_TOOL_TRIPLETAP));
  assert(touch_device_handler_.EnableKeyEvent(BTN_TOOL_QUADTAP));

  touch_device_handler_.EnableEventType(EV_ABS);
  AbsInit(ABS_X, 0, kXMax, kXRes);
  AbsInit(ABS_Y, 0, kYMax, kYRes);
  AbsInit(ABS_PRESSURE, 0, 255, 0);
  AbsInit(ABS_MT_SLOT, 0, kMaxFingers - 1, 0);
  AbsInit(ABS_MT_TRACKING_ID, 0, 0xffff, 0);
  AbsInit(ABS_MT_POSITION_X, 0, kXMax, kXRes);
  AbsInit(ABS_MT_POSITION_Y, 0, kYMax, kYRes);
  AbsInit(ABS_MT_PRESSURE, 0, 255, 0);
  assert(touch_device_handler_.FinalizeUinputCreation("userspace-touchpad"));
}

void TouchEmulator::FlushEvents(const std::vector<TouchEvent>& fingers,
                                const bool button_down) {
  int min_active_slot = kMaxFingers;
  int min_active_slot_finger_idx = -1;
  int new_slot_tid[kMaxFingers];
  int current_finger_count = 0;

  for (size_t i = 0; i < kMaxFingers; i++)
    new_slot_tid[i] = -1;

  // Process finger one-by-one.
  for (size_t i = 0; i < fingers.size(); i++) {
    int slot = -1;
    size_t j;
    const TouchEvent& finger = fingers[i];

    // Skip if the finger is leaving.
    if (finger.tracking_id == -1)
      continue;

    // See if the finger is present in the previous frame.
    for (j = 0; j < kMaxFingers; ++j) {
      if (finger.tracking_id == slot_tid_[j])
        break;
      // Get the first free slot if possible.
      if (slot == -1 && slot_tid_[j] == -1)
        slot = j;
    }

    // Use the matched previous finger's slot if available.
    if (j != kMaxFingers)
      slot = j;

    // Record the finger with minimum slot index. This is used to identify the
    // finger that we will use to send ABS_X and ABS_Y events.
    if (slot < min_active_slot) {
      min_active_slot = slot;
      min_active_slot_finger_idx = i;
    }

    // Update and send events for this finger.
    current_finger_count++;
    new_slot_tid[slot] = slot_tid_[slot] = finger.tracking_id;
    WriteTouchEvent(slot, finger.tracking_id, finger.x, finger.y,
                    finger.pressure);
  }

  // Check if any finger left, emit tracking id change if necessary.
  for (size_t i = 0; i < kMaxFingers; i++) {
    if (slot_tid_[i] != -1 && new_slot_tid[i] == -1) {
      slot_tid_[i] = -1;
      WriteTouchEvent(i, -1, 0, 0, 0);
    }
  }

  // Send multi-touch button events.
  WriteTouchButtonEvent(current_finger_count);

  // Send single touch events for backward compatibility.
  if (current_finger_count >= 1) {
    assert(touch_device_handler_.SendEvent(
        EV_ABS, ABS_X, fingers[min_active_slot_finger_idx].x));
    assert(touch_device_handler_.SendEvent(
        EV_ABS, ABS_Y, fingers[min_active_slot_finger_idx].y));
    assert(touch_device_handler_.SendEvent(
        EV_ABS, ABS_PRESSURE, fingers[min_active_slot_finger_idx].pressure));
  }

  // Send the physical button event.
  assert(
      touch_device_handler_.SendEvent(EV_KEY, BTN_LEFT, button_down ? 1 : 0));

  // Conclude the input report.
  assert(touch_device_handler_.SendEvent(EV_SYN, SYN_REPORT, 0));
}

void TouchEmulator::WriteTouchEvent(
    int slot, int id, int x, int y, int pressure) {
  assert(touch_device_handler_.SendEvent(EV_ABS, ABS_MT_SLOT, slot));
  assert(touch_device_handler_.SendEvent(EV_ABS, ABS_MT_TRACKING_ID, id));
  if (id != -1) {
    assert(touch_device_handler_.SendEvent(EV_ABS, ABS_MT_POSITION_X, x));
    assert(touch_device_handler_.SendEvent(EV_ABS, ABS_MT_POSITION_Y, y));
    assert(touch_device_handler_.SendEvent(EV_ABS, ABS_MT_PRESSURE, pressure));
  }
}

void TouchEmulator::WriteTouchButtonEvent(int finger_count) {
  // Just send all events repetitively and let kernel's delta compression
  // handles it.
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOUCH, finger_count > 0));
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOOL_FINGER,
                                         finger_count == 1));
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOOL_DOUBLETAP,
                                         finger_count == 2));
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOOL_TRIPLETAP,
                                         finger_count == 3));
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOOL_QUADTAP,
                                         finger_count == 4));
  assert(touch_device_handler_.SendEvent(EV_KEY, BTN_TOOL_QUINTTAP,
                                         finger_count == 5));
}
