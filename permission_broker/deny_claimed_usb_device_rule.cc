// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/deny_claimed_usb_device_rule.h"

#include <libudev.h>

#include <memory>
#include <string>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "permission_broker/udev_scopers.h"

using policy::DevicePolicy;

namespace {

const uint32_t kAdbClass = 0xff;
const uint32_t kAdbSubclass = 0x42;
const uint32_t kAdbProtocol = 0x1;

enum class RemovableAttr {
  kUnknown,
  kFixed,
  kRemovable,
};

RemovableAttr ParseRemovableSysattr(const std::string& removable) {
  if (removable == "fixed") {
    return RemovableAttr::kFixed;
  } else if (removable == "removable") {
    return RemovableAttr::kRemovable;
  } else {
    if (removable != "unknown") {
      LOG(WARNING) << "Unexpected value for removable sysattr: '" << removable
                   << "'";
    }
    return RemovableAttr::kUnknown;
  }
}

RemovableAttr GetRemovableSysattr(udev_device* device) {
  const char* removable = udev_device_get_sysattr_value(device, "removable");
  if (!removable) {
    return RemovableAttr::kUnknown;
  }
  return ParseRemovableSysattr(removable);
}

bool GetUIntSysattr(udev_device* device, const char* key, uint32_t* val) {
  CHECK(val);

  const char* str_val = udev_device_get_sysattr_value(device, key);
  return str_val && base::HexStringToUInt(str_val, val);
}

}  // namespace

namespace permission_broker {

DenyClaimedUsbDeviceRule::DenyClaimedUsbDeviceRule()
    : UsbSubsystemUdevRule("DenyClaimedUsbDeviceRule"), policy_loaded_(false) {}

DenyClaimedUsbDeviceRule::~DenyClaimedUsbDeviceRule() = default;

bool DenyClaimedUsbDeviceRule::LoadPolicy() {
  usb_allow_list_.clear();

  auto policy_provider = std::make_unique<policy::PolicyProvider>();
  policy_provider->Reload();

  // No available policies.
  if (!policy_provider->device_policy_is_loaded())
    return false;

  const policy::DevicePolicy* policy = &policy_provider->GetDevicePolicy();
  return policy->GetUsbDetachableWhitelist(&usb_allow_list_);
}

bool DenyClaimedUsbDeviceRule::IsDeviceDetachableByPolicy(udev_device* device) {
  // Retrieve the device policy for detachable USB devices if needed.
  if (!policy_loaded_)
    policy_loaded_ = LoadPolicy();
  if (!policy_loaded_)
    return false;

  // Check whether this USB device is allowed.
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  for (const DevicePolicy::UsbDeviceId& id : usb_allow_list_) {
    if (id.vendor_id == vendor_id &&
        (!id.product_id || id.product_id == product_id))
      return true;
  }

  return false;
}

bool DenyClaimedUsbDeviceRule::IsInterfaceAdb(udev_device* device) {
  uint32_t intf_class, intf_subclass, intf_protocol;
  if (!GetUIntSysattr(device, "bInterfaceClass", &intf_class) ||
      !GetUIntSysattr(device, "bInterfaceSubClass", &intf_subclass) ||
      !GetUIntSysattr(device, "bInterfaceProtocol", &intf_protocol))
    return false;

  return intf_class == kAdbClass && intf_subclass == kAdbSubclass &&
         intf_protocol == kAdbProtocol;
}

bool IsInterfaceUsbSerial(udev_device* iface) {
  // Search all children of the interface in the 'usb-serial' subsystem
  // this includes all the USB-serial converter and most micro-controllers
  // USB bulk endpoints presenting a serial-like interface but not CDC-ACM
  // devices (e.g. modems or boards pretending to be one)
  udev* udev = udev_device_get_udev(iface);
  ScopedUdevEnumeratePtr enum_serial(udev_enumerate_new(udev));
  udev_enumerate_add_match_subsystem(enum_serial.get(), "usb-serial");
  udev_enumerate_add_match_parent(enum_serial.get(), iface);
  udev_enumerate_scan_devices(enum_serial.get());

  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enum_serial.get())) {
    // a usb-serial driver is connected to this interface
    LOG(INFO) << "Found usb-serial interface.";
    return true;
  }
  return false;
}

bool IsInterfaceSafeToDetach(udev_device* iface) {
  // Normally the permission_broker prevents users from interfering with the
  // system usage of a USB device.
  // But in particular cases, a USB interface is deemed 'safe to detach' from
  // its kernel driver if the purpose of the driver is only exposing it to apps.
  // e.g. below the usb serial interfaces are only used by the chrome.serial
  // and WebSerial external API rather than in any intrinsic system use.
  return IsInterfaceUsbSerial(iface);
}

bool IsDeviceAllowedSerial(udev_device* device) {
  // These vendor IDs are derived from https://raw.githubusercontent.com
  // /arduino/ArduinoCore-avr/master/boards.txt
  // /arduino/ArduinoCore-sam/master/boards.txt
  // /arduino/ArduinoCore-samd/master/boards.txt
  // using
  // grep -o -E  "vid\..*=(0x.*)" *boards.txt | sed "s/vid\..=//g" | sort -f | \
  // uniq -i
  const uint32_t kArduinoVendorIds[] = {0x2341, 0x1b4f, 0x239a, 0x2a03, 0x10c4};
  const uint32_t kGoogleVendorId = 0x18d1;
  uint32_t vendor_id, product_id;
  if (!GetUIntSysattr(device, "idVendor", &vendor_id) ||
      !GetUIntSysattr(device, "idProduct", &product_id))
    return false;

  if (vendor_id == kGoogleVendorId) {
    switch (product_id) {
      case 0x5002:  // Servo V2
      case 0x5003:  // Servo V2
      case 0x500a:  // twinkie
      case 0x500b:  // Plankton
      case 0x500c:  // Plankton
      case 0x5014:  // Cr50
      case 0x501a:  // Servo micro
      case 0x501b:  // Servo V4
      case 0x501f:  // Suzyq
      case 0x5020:  // Sweetberry
      case 0x5027:  // Tigertail
      case 0x5036:  // Chocodile
        return true;
    }
  }

  for (const auto& arduino_vendor_id : kArduinoVendorIds) {
    if (vendor_id == arduino_vendor_id) {
      return true;
    }
  }
  return false;
}

Rule::Result DenyClaimedUsbDeviceRule::ProcessUsbDevice(udev_device* device) {
  const char* device_syspath = udev_device_get_syspath(device);
  if (!device_syspath) {
    return DENY;
  }

  udev* udev = udev_device_get_udev(device);
  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev));
  udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
  udev_enumerate_scan_devices(enumerate.get());

  bool found_claimed_interface = false;
  bool found_unclaimed_interface = false;
  bool found_adb_interface = false;
  bool found_only_safe_interfaces = true;
  struct udev_list_entry* entry = nullptr;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(entry);
    ScopedUdevDevicePtr child(udev_device_new_from_syspath(udev, entry_path));

    // Find out if this entry's direct parent is the device in question.
    struct udev_device* parent = udev_device_get_parent(child.get());
    if (!parent) {
      continue;
    }
    const char* parent_syspath = udev_device_get_syspath(parent);
    if (!parent_syspath || strcmp(device_syspath, parent_syspath) != 0) {
      continue;
    }

    const char* child_type = udev_device_get_devtype(child.get());
    if (!child_type || strcmp(child_type, "usb_interface") != 0) {
      // If this is not a usb_interface node then something is wrong, fail safe.
      LOG(WARNING) << "Found a child '" << entry_path
                   << "' with unexpected type: "
                   << (child_type ? child_type : "(null)");
      return DENY;
    }

    const char* driver = udev_device_get_driver(child.get());
    if (driver) {
      LOG(INFO) << "Found claimed interface with driver: " << driver;
      found_claimed_interface = true;
      found_only_safe_interfaces =
          found_only_safe_interfaces && IsInterfaceSafeToDetach(child.get());
    } else {
      found_unclaimed_interface = true;
    }

    if (IsInterfaceAdb(child.get())) {
      LOG(INFO) << "Found ADB interface.";
      found_adb_interface = true;
    }
  }

  if (found_claimed_interface) {
    // Don't allow detaching the driver from fixed (internal) USB devices.
    if (GetRemovableSysattr(device) == RemovableAttr::kFixed) {
      LOG(INFO) << "Denying fixed USB device with driver.";
      return DENY;
    }

    if (found_only_safe_interfaces)
      LOG(INFO) << "Found only detachable interface(s), safe to claim.";

    if (IsDeviceDetachableByPolicy(device) || IsDeviceAllowedSerial(device) ||
        found_adb_interface || found_only_safe_interfaces)
      return ALLOW_WITH_DETACH;
    else
      return found_unclaimed_interface ? ALLOW_WITH_LOCKDOWN : DENY;
  } else {
    return IGNORE;
  }
}

}  // namespace permission_broker
