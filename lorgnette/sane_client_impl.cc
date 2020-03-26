// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <map>

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <sane/saneopts.h>

namespace lorgnette {

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create() {
  SANE_Status status = sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl());
}

SaneClientImpl::~SaneClientImpl() {
  sane_exit();
}

bool SaneClientImpl::ListDevices(brillo::ErrorPtr* error,
                                 Manager::ScannerInfo* info_out) {
  base::AutoLock auto_lock(lock_);
  const SANE_Device** device_list;
  SANE_Status status = sane_get_devices(&device_list, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get device list from SANE");
    return false;
  }

  return DeviceListToScannerInfo(device_list, info_out);
}

// static
bool SaneClientImpl::DeviceListToScannerInfo(const SANE_Device** device_list,
                                             Manager::ScannerInfo* info_out) {
  if (!device_list || !info_out) {
    return false;
  }

  Manager::ScannerInfo scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0)
      continue;

    if (scanners.count(dev->name) != 0)
      return false;

    std::map<std::string, std::string> scanner_info;
    scanner_info[kScannerPropertyManufacturer] = dev->vendor ? dev->vendor : "";
    scanner_info[kScannerPropertyModel] = dev->model ? dev->model : "";
    scanner_info[kScannerPropertyType] = dev->type ? dev->type : "";
    scanners[dev->name] = scanner_info;
  }
  *info_out = scanners;
  return true;
}

SaneClientImpl::SaneClientImpl() {}

}  // namespace lorgnette
