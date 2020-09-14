// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_IPPUSB_DEVICE_H_
#define LORGNETTE_IPPUSB_DEVICE_H_

#include <string>
#include <vector>

#include <base/optional.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette {

// Convert an ippusb backend name to a real backend string, e.g.
// ippusb:escl:Device:1234_5678/eSCL/ to
// airscan:escl:Device:unix://1234_5678.sock/eSCL/.  In the process, contacts
// ippusb_manager to find a matching devices and create an IPP-USB tunnel to it.
// Returns base::nullopt if the device can't be found or an error occurs
// starting the tunnel.
base::Optional<std::string> BackendForDevice(const std::string& device_name);

// Get a list of potential eSCL-over-USB devices attached to the system.  Each
// returned device will be a printer that claims to support IPP-USB, but they
// are not probed for eSCL support.  The caller must double-check returned
// devices before using them to scan.
std::vector<ScannerInfo> FindIppUsbDevices();

}  // namespace lorgnette

#endif  // LORGNETTE_IPPUSB_DEVICE_H_
