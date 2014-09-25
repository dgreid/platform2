// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libudev.h>

#include <string>

#include "base/logging.h"
#include "permission_broker/deny_claimed_hidraw_device_rule.h"
#include "permission_broker/udev_scopers.h"

namespace permission_broker {

namespace {

const char kLogitechUnifyingReceiverDriver[] = "logitech-djreceiver";

}

DenyClaimedHidrawDeviceRule::DenyClaimedHidrawDeviceRule()
    : HidrawSubsystemUdevRule("DenyClaimedHidrawDeviceRule") {}

Rule::Result DenyClaimedHidrawDeviceRule::ProcessHidrawDevice(
    struct udev_device *device) {
  // For now, treat non-USB HID devices as claimed.
  struct udev_device* usb_interface =
      udev_device_get_parent_with_subsystem_devtype(
          device, "usb", "usb_interface");
  if (!usb_interface) {
    return DENY;
  }

  // Add an exception to the rule for Logitech Unifying receiver.
  // This hidraw device is a parent of devices that have input
  // subsystem. Yet the traffic to those children is not available on
  // the hidraw node of the receiver, so it is safe to white-list it.
  struct udev_device* hid_parent =
      udev_device_get_parent_with_subsystem_devtype(device, "hid", NULL);
  if (hid_parent) {
    const char* hid_parent_driver = udev_device_get_driver(hid_parent);
    if (strcmp(hid_parent_driver, kLogitechUnifyingReceiverDriver) == 0) {
      LOG(INFO) << "Found Logitech Unifying receiver. Skipping rule.";
      return IGNORE;
    }
  }

  std::string usb_interface_path(udev_device_get_syspath(usb_interface));

  // Scan all children of the USB interface for subsystems other than generic
  // USB or HID. The presence of such subsystems is an indication that the
  // device is in use by another driver.
  //
  // Because udev lacks the ability to filter an enumeration by arbitrary
  // ancestor properties (e.g. "enumerate all nodes with a usb_interface
  // ancestor") we have to scan the entire set of devices to find potential
  // matches.
  struct udev* udev = udev_device_get_udev(device);
  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev));
  udev_enumerate_scan_devices(enumerate.get());
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* syspath = udev_list_entry_get_name(entry);
    ScopedUdevDevicePtr child(udev_device_new_from_syspath(udev, syspath));
    struct udev_device* child_usb_interface =
        udev_device_get_parent_with_subsystem_devtype(
            child.get(), "usb", "usb_interface");
    if (!child_usb_interface) {
      continue;
    }
    std::string child_usb_interface_path(
        udev_device_get_syspath(child_usb_interface));
    // This device shares a USB interface with the hidraw device in question.
    // Check its subsystem to see if it should block hidraw access.
    if (child_usb_interface_path == usb_interface_path) {
      const char* subsystem = udev_device_get_subsystem(child.get());
      if (ShouldSiblingSubsystemExcludeHidAccess(subsystem)) {
        return DENY;
      }
    }
  }

  return IGNORE;
}

bool DenyClaimedHidrawDeviceRule::ShouldSiblingSubsystemExcludeHidAccess(
    const char *subsystem) {
  // Return true iff subsystem something other than NULL or generic USB/HID.
  return subsystem &&
    strcmp(subsystem, "hid") &&
    strcmp(subsystem, "hidraw") &&
    strcmp(subsystem, "usb") &&
    strcmp(subsystem, "usbmisc");
}

}  // namespace permission_broker
