// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef USERSPACE_TOUCHPAD_UINPUTDEVICE_H_
#define USERSPACE_TOUCHPAD_UINPUTDEVICE_H_

#include <error.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <unistd.h>

#include "userspace_touchpad/syscallhandler.h"

#define UINPUT_CONTROL_FILENAME "/dev/uinput"

// When creating a new uinput device, you must specify these parameters like
// with an actual, physical device.  These are sane, safe values.
#define GOOGLE_VENDOR_ID 0x18d1
#define DUMMY_PRODUCT_ID 0x00FF
#define VERSION_NUMBER 1

// Macros used for the bit manipulations required to interpret the results of
// ioctls when querying the capabilities of a device to duplicate with uinput.
#define BITS_PER_LONG (sizeof(int64_t) * 8)
#define NBITS(x) ((((x)-1) / BITS_PER_LONG) + 1)
#define OFF(x) ((x) % BITS_PER_LONG)
#define BIT(x) (1UL << OFF(x))
#define LONG(x) ((x) / BITS_PER_LONG)
#define test_bit(bit, array) ((array[LONG(bit)] >> OFF(bit)) & 1)

// Define these structs and ioctls so this can compile against an old kernel
// that doesn't support these features yet.  The kernel this code runs on
// must support it, but adding these here allows it to build elsewhere.
// Without these, uinput can't correctly support setting the axis ranges for
// absolute axes which is essential for simulated touch devices.
#ifndef UI_ABS_SETUP
#define UI_ABS_SETUP _IOW(UINPUT_IOCTL_BASE, 4, struct uinput_abs_setup)
struct uinput_abs_setup {
  __u16 code; /* axis code */
  /* __u16 filler; */
  struct input_absinfo absinfo;
};
#endif
#ifndef UI_DEV_SETUP
#define UI_DEV_SETUP _IOW(UINPUT_IOCTL_BASE, 3, struct uinput_setup)
struct uinput_setup {
  struct input_id id;
  char name[UINPUT_MAX_NAME_SIZE];
  __u32 ff_effects_max;
};
#endif

class UinputDevice {
  /* A class to allow you to easily create uinput devices and generate events.
   *
   * This class can be used to create uinput devices, setup which events they
   * are capable of generating, and actually sending them.  The general flow is
   * to instantiate a UinputDevice object and call CreateUinputFD() to get the
   * process started.  You can then use the various EnableXXXX() functions to
   * enable the correct event types that you plan to generate.  Once all the
   * events are enabled, FinalizeUinputCreation() will tell the kernel create
   * the device and SendEvent() can now be used.
   */
 public:
  UinputDevice()
      : syscall_handler_(&default_syscall_handler_), uinput_fd_(-1) {}
  explicit UinputDevice(SyscallHandler* syscall_handler)
      : syscall_handler_(syscall_handler), uinput_fd_(-1) {
    // This constructor allows you to pass in a SyscallHandler when unit
    // testing this class.  For real use, allow it to use the default value
    // by using the constructor with no arguments.
    if (syscall_handler_ == NULL) {
      syscall_handler_ = &default_syscall_handler_;
    }
  }
  ~UinputDevice();

  bool CreateUinputFD();

  bool EnableEventType(int ev_type) const;
  bool EnableKeyEvent(int ev_code) const;
  bool EnableAbsEvent(int ev_code) const;
  bool FinalizeUinputCreation(std::string const& device_name) const;
  bool CopyABSOutputEvents(int source_evdev_fd, int width, int height) const;
  bool SendEvent(int ev_type, int ev_code, int value) const;

  int get_fd() const { return uinput_fd_; }

 private:
  SyscallHandler default_syscall_handler_;
  SyscallHandler* syscall_handler_;
  int uinput_fd_;

  friend class UinputDeviceTest;
};

#endif  // USERSPACE_TOUCHPAD_UINPUTDEVICE_H_
