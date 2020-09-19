// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gaming-input-unstable-v2-client-protocol.h"  // NOLINT(build/include_directory)

// Overview of state management via gaming events, in order:
// 1) Acquire gaming seats (in sommelier.cc)
// 2) Add listeners to gaming seats
// 3) Listen for zcr_gaming_seat_v2.gamepad_added to construct a 'default'
//    game controller (not currently implemented)
//    Calls libevdev_new, libevdev_enable_event_type,
//          libevdev_uinput_create_from_device
// 4) Listen for zcr_gaming_seat_v2.gamepad_added_with_device_info to construct
//    a custom game controller
//    Calls libevdev_new
// 5) Listen for zcr_gamepad_v2.axis_added to fill in a custom game controller
//    Calls libevdev_enable_event_type
// 6) Listen for zcr_gamepad_v2.activated to finalize a custom game controller
//    Calls libevdev_uinput_create_from_device
// 7) Listen for zcr_gamepad_v2.axis to set frame state for game controller
//    Calls libevdev_uinput_write_event
// 8) Listen for zcr_gamepad_v2.button to set frame state for game controller
//    Calls libevdev_uinput_write_event
// 9) Listen for zcr_gamepad_v2.frame to emit collected frame
//    Calls libevdev_uinput_write_event(EV_SYN)
// 10) Listen for zcr_gamepad_v2.removed to destroy gamepad
//    Must handle gamepads in all states of construction or error

enum GamepadActivationState {
  kStateUnknown = 0,    // Should not happen
  kStatePending = 1,    // Constructed, pending axis definition
  kStateActivated = 2,  // Fully activated
  kStateError = 3       // Error occurred during construction; ignore gracefully
};

const char kXboxName[] = "Microsoft X-Box One S pad";
const uint32_t kUsbBus = 0x03;
const uint32_t kXboxVendor = 0x45e;
const uint32_t kXboxProduct = 0x2ea;
const uint32_t kXboxVersion = 0x301;

const uint32_t kStadiaVendor = 0x18d1;
const uint32_t kStadiaProduct = 0x9400;
const uint32_t kStadiaVersion = 0x111;

// Note: the majority of protocol errors are treated as non-fatal, and
// are intended to be handled gracefully, as is removal at any
// state of construction or operation. We should expect that
// 'sudden removal' can happen at any time, due to hotplugging
// or unexpected state changes from the wayland server.

static void sl_internal_gamepad_removed(void* data,
                                        struct zcr_gamepad_v2* gamepad) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  assert(host_gamepad->state == kStatePending ||
         host_gamepad->state == kStateActivated ||
         host_gamepad->state == kStateError);

  if (host_gamepad->uinput_dev != NULL)
    libevdev_uinput_destroy(host_gamepad->uinput_dev);
  if (host_gamepad->ev_dev != NULL)
    libevdev_free(host_gamepad->ev_dev);

  zcr_gamepad_v2_destroy(gamepad);

  wl_list_remove(&host_gamepad->link);
  free(host_gamepad);
}

static uint32_t remap_axis(struct sl_host_gamepad* host_gamepad,
                           uint32_t axis) {
  if (host_gamepad->stadia) {
    if (axis == ABS_Z)
      axis = ABS_RX;
    else if (axis == ABS_RZ)
      axis = ABS_RY;
    else if (axis == ABS_BRAKE)
      axis = ABS_Z;
    else if (axis == ABS_GAS)
      axis = ABS_RZ;
  }
  return axis;
}

static void sl_internal_gamepad_axis(void* data,
                                     struct zcr_gamepad_v2* gamepad,
                                     uint32_t time,
                                     uint32_t axis,
                                     wl_fixed_t value) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  axis = remap_axis(host_gamepad, axis);

  // Note: incoming time is ignored, it will be regenerated from current time.
  libevdev_uinput_write_event(host_gamepad->uinput_dev, EV_ABS, axis,
                              wl_fixed_to_double(value));
}

static void sl_internal_gamepad_button(void* data,
                                       struct zcr_gamepad_v2* gamepad,
                                       uint32_t time,
                                       uint32_t button,
                                       uint32_t state,
                                       wl_fixed_t analog) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  // Note: Exo wayland server always sends analog==0, only pay attention
  // to state.
  int value = (state == ZCR_GAMEPAD_V2_BUTTON_STATE_PRESSED) ? 1 : 0;

  // Note: incoming time is ignored, it will be regenerated from current time.
  libevdev_uinput_write_event(host_gamepad->uinput_dev, EV_KEY, button, value);
}

static void sl_internal_gamepad_frame(void* data,
                                      struct zcr_gamepad_v2* gamepad,
                                      uint32_t time) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  // Note: incoming time is ignored, it will be regenerated from current time.
  libevdev_uinput_write_event(host_gamepad->uinput_dev, EV_SYN, SYN_REPORT, 0);
}

static void sl_internal_gamepad_axis_added(void* data,
                                           struct zcr_gamepad_v2* gamepad,
                                           uint32_t index,
                                           int32_t min_value,
                                           int32_t max_value,
                                           int32_t flat,
                                           int32_t fuzz,
                                           int32_t resolution) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;
  struct input_absinfo info = {.value = 0,  // Does this matter?
                               .minimum = min_value,
                               .maximum = max_value,
                               .fuzz = fuzz,
                               .flat = flat,
                               .resolution = resolution};

  if (host_gamepad->state != kStatePending) {
    fprintf(stderr, "error: %s invoked in unexpected state %d\n", __func__,
            host_gamepad->state);
    host_gamepad->state = kStateError;
    return;
  }

  index = remap_axis(host_gamepad, index);

  libevdev_enable_event_code(host_gamepad->ev_dev, EV_ABS, index, &info);
}

static void sl_internal_gamepad_activated(void* data,
                                          struct zcr_gamepad_v2* gamepad) {
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStatePending) {
    fprintf(stderr, "error: %s invoked in unexpected state %d\n", __func__,
            host_gamepad->state);
    host_gamepad->state = kStateError;
    return;
  }

  int err = libevdev_uinput_create_from_device(host_gamepad->ev_dev,
                                               LIBEVDEV_UINPUT_OPEN_MANAGED,
                                               &host_gamepad->uinput_dev);
  if (err == 0) {
    // TODO(kenalba): can we destroy and clean up the ev_dev now?
    host_gamepad->state = kStateActivated;
  } else {
    fprintf(stderr,
            "error: libevdev_uinput_create_from_device failed with error %d\n",
            err);
    host_gamepad->state = kStateError;
  }
}

static void sl_internal_gamepad_vibrator_added(
    void* data,
    struct zcr_gamepad_v2* gamepad,
    struct zcr_gamepad_vibrator_v2* vibrator) {
  // TODO(kenalba): add vibration logic
}

static const struct zcr_gamepad_v2_listener sl_internal_gamepad_listener = {
    sl_internal_gamepad_removed,       sl_internal_gamepad_axis,
    sl_internal_gamepad_button,        sl_internal_gamepad_frame,
    sl_internal_gamepad_axis_added,    sl_internal_gamepad_activated,
    sl_internal_gamepad_vibrator_added};

static void sl_internal_gaming_seat_gamepad_added_with_device_info(
    void* data,
    struct zcr_gaming_seat_v2* gaming_seat,
    struct zcr_gamepad_v2* gamepad,
    const char* name,
    uint32_t bus,
    uint32_t vendor_id,
    uint32_t product_id,
    uint32_t version) {
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_gamepad* host_gamepad =
      static_cast<sl_host_gamepad*>(malloc(sizeof(struct sl_host_gamepad)));
  assert(host_gamepad);
  wl_list_insert(&ctx->gamepads, &host_gamepad->link);
  zcr_gamepad_v2_set_user_data(gamepad, host_gamepad);
  zcr_gamepad_v2_add_listener(gamepad, &sl_internal_gamepad_listener,
                              host_gamepad);

  host_gamepad->ctx = ctx;
  host_gamepad->state = kStatePending;
  host_gamepad->ev_dev = libevdev_new();
  host_gamepad->uinput_dev = NULL;
  host_gamepad->stadia = false;

  if (host_gamepad->ev_dev == NULL) {
    fprintf(stderr, "error: libevdev_new failed\n");
    host_gamepad->state = kStateError;
    return;
  }

  // We provide limited remapping at this time. Only moderately XBox360
  // HID compatible controllers are likely to work well.

  if (product_id == kStadiaProduct && vendor_id == kStadiaVendor &&
      version == kStadiaVersion) {
    host_gamepad->stadia = true;
  }

  // Describe a common controller
  libevdev_set_name(host_gamepad->ev_dev, kXboxName);
  libevdev_set_id_bustype(host_gamepad->ev_dev, kUsbBus);
  libevdev_set_id_vendor(host_gamepad->ev_dev, kXboxVendor);
  libevdev_set_id_product(host_gamepad->ev_dev, kXboxProduct);
  libevdev_set_id_version(host_gamepad->ev_dev, kXboxVersion);

  // Enable common set of buttons

  // Note: Do not enable BTN_TL2 or BTN_TR2, as they will significantly
  // change the Linux joydev interpretation of the triggers on ABS_Z/ABS_RZ.
  int buttons[] = {BTN_SOUTH,  BTN_EAST,  BTN_NORTH,  BTN_WEST,
                   BTN_TL,     BTN_TR,    BTN_THUMBL, BTN_THUMBR,
                   BTN_SELECT, BTN_START, BTN_MODE};

  for (unsigned int i = 0; i < ARRAY_SIZE(buttons); i++)
    libevdev_enable_event_code(host_gamepad->ev_dev, EV_KEY, buttons[i], NULL);
}  // NOLINT(whitespace/indent), lint bug b/173143790

// Note: not currently implemented by Exo.
static void sl_internal_gaming_seat_gamepad_added(
    void* data,
    struct zcr_gaming_seat_v2* gaming_seat,
    struct zcr_gamepad_v2* gamepad) {
  fprintf(stderr,
          "error: sl_internal_gaming_seat_gamepad_added unimplemented\n");
}

static const struct zcr_gaming_seat_v2_listener
    sl_internal_gaming_seat_listener = {
        sl_internal_gaming_seat_gamepad_added,
        sl_internal_gaming_seat_gamepad_added_with_device_info};

void sl_gaming_seat_add_listener(struct sl_context* ctx) {
  if (ctx->gaming_input_manager && ctx->gaming_input_manager->internal) {
    // TODO(kenalba): does gaming_seat need to persist in ctx?
    struct zcr_gaming_seat_v2* gaming_seat =
        zcr_gaming_input_v2_get_gaming_seat(ctx->gaming_input_manager->internal,
                                            ctx->default_seat->proxy);
    zcr_gaming_seat_v2_add_listener(gaming_seat,
                                    &sl_internal_gaming_seat_listener, ctx);
  }
}
