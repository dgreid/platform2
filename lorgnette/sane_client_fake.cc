// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_fake.h"

#include <map>

#include <chromeos/dbus/service_constants.h>

namespace lorgnette {

// static
bool SaneClientFake::ListDevices(brillo::ErrorPtr* error,
                                 Manager::ScannerInfo* info_out) {
  base::AutoLock auto_lock(lock_);

  if (!list_devices_result_)
    return false;

  *info_out = scanners_;
  return true;
}

void SaneClientFake::SetListDevicesResult(bool value) {
  list_devices_result_ = value;
}

void SaneClientFake::AddDevice(const std::string& name,
                               const std::string& manufacturer,
                               const std::string& model,
                               const std::string& type) {
  std::map<std::string, std::string> scanner_info;
  scanner_info[kScannerPropertyManufacturer] = manufacturer;
  scanner_info[kScannerPropertyModel] = model;
  scanner_info[kScannerPropertyType] = type;

  scanners_[name] = scanner_info;
}

void SaneClientFake::RemoveDevice(const std::string& name) {
  scanners_.erase(name);
}

}  // namespace lorgnette
