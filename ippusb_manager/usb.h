// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPPUSB_MANAGER_USB_H_
#define IPPUSB_MANAGER_USB_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <libusb.h>

namespace ippusb_manager {

class UsbPrinterInfo {
 public:
  static std::unique_ptr<UsbPrinterInfo> Create(uint16_t vid, uint16_t pid);

  // Simple getter methods.
  uint16_t vid() const { return vid_; }
  uint16_t pid() const { return pid_; }
  uint8_t bus() const { return bus_; }
  uint8_t device() const { return device_; }

  // Simple setter methods.
  void set_bus(uint8_t bus) { bus_ = bus; }
  void set_device(uint8_t device) { device_ = device; }

  // Searches the connected Usb devices to determine if there is a connected
  // device which matches the |vid_| and |pid_| of this UsbPrinterInfo. If there
  // is a match then the location of where the device was found (bus/device) are
  // set in |bus_| and |device_| and the function returns true.
  bool FindDeviceLocation();

 private:
  UsbPrinterInfo(uint16_t vid, uint16_t pid);
  UsbPrinterInfo(const UsbPrinterInfo&) = delete;
  UsbPrinterInfo& operator=(const UsbPrinterInfo&) = delete;

  uint16_t vid_;
  uint16_t pid_;
  uint8_t bus_;
  uint8_t device_;
};

// Parses a given string of the format "<vid> <pid>" where the values in the
// string represent hexidecimal integers. If the message is parsed successfully
// then the values integer values will be stored in |vid| and |pid| and the
// function will return true.
bool GetUsbInfo(const std::string& info, uint16_t* vid, uint16_t* pid);

}  // namespace ippusb_manager

#endif  // IPPUSB_MANAGER_USB_H_
