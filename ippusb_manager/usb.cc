// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ippusb_manager/usb.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <libusb.h>

namespace ippusb_manager {

bool GetUsbInfo(const std::string& info, uint16_t* vid, uint16_t* pid) {
  auto tokens =
      base::SplitString(info, "_", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (tokens.size() != 2)
    return false;

  int v, p;
  if (!base::HexStringToInt(tokens[0], &v) ||
      !base::HexStringToInt(tokens[1], &p))
    return false;

  *vid = v;
  *pid = p;

  return true;
}

UsbPrinterInfo::UsbPrinterInfo(uint16_t vid, uint16_t pid)
    : vid_(vid), pid_(pid) {}

bool UsbPrinterInfo::FindDeviceLocation() {
  libusb_device_handle* handle =
      libusb_open_device_with_vid_pid(nullptr, vid_, pid_);

  if (handle) {
    libusb_device* device = libusb_get_device(handle);
    set_bus(libusb_get_bus_number(device));
    set_device(libusb_get_device_address(device));
    libusb_close(handle);
    return true;
  }

  return false;
}

std::unique_ptr<UsbPrinterInfo> UsbPrinterInfo::Create(uint16_t vid,
                                                       uint16_t pid) {
  return std::unique_ptr<UsbPrinterInfo>(new UsbPrinterInfo(vid, pid));
}

}  // namespace ippusb_manager
