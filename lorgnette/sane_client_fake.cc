// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_fake.h"

#include <algorithm>
#include <map>
#include <utility>

#include <chromeos/dbus/service_constants.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"

static const char* kDbusDomain = brillo::errors::dbus::kDomain;

namespace lorgnette {

// static
bool SaneClientFake::ListDevices(brillo::ErrorPtr* error,
                                 std::vector<ScannerInfo>* scanners_out) {
  if (!list_devices_result_)
    return false;

  *scanners_out = scanners_;
  return true;
}

std::unique_ptr<SaneDevice> SaneClientFake::ConnectToDeviceInternal(
    brillo::ErrorPtr* error, const std::string& device_name) {
  if (devices_.count(device_name) > 0) {
    auto ptr = std::move(devices_[device_name]);
    devices_.erase(device_name);
    return ptr;
  }

  brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                       "No device");
  return nullptr;
}

void SaneClientFake::SetListDevicesResult(bool value) {
  list_devices_result_ = value;
}

void SaneClientFake::AddDevice(const std::string& name,
                               const std::string& manufacturer,
                               const std::string& model,
                               const std::string& type) {
  ScannerInfo info;
  info.set_name(name);
  info.set_manufacturer(manufacturer);
  info.set_model(model);
  info.set_type(type);
  scanners_.push_back(info);
}

void SaneClientFake::RemoveDevice(const std::string& name) {
  for (auto it = scanners_.begin(); it != scanners_.end(); it++) {
    if (it->name() == name) {
      scanners_.erase(it);
    }
  }
}

void SaneClientFake::SetDeviceForName(const std::string& device_name,
                                      std::unique_ptr<SaneDeviceFake> device) {
  devices_.emplace(device_name, std::move(device));
}

SaneDeviceFake::SaneDeviceFake()
    : resolution_(100),
      start_scan_result_(SANE_STATUS_GOOD),
      read_scan_data_result_(SANE_STATUS_GOOD),
      scan_running_(false) {}

SaneDeviceFake::~SaneDeviceFake() {}

bool SaneDeviceFake::GetValidOptionValues(brillo::ErrorPtr* error,
                                          ValidOptionValues* values) {
  if (!values || !values_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No option values");
    return false;
  }

  *values = values_.value();
  return true;
}

bool SaneDeviceFake::GetScanResolution(brillo::ErrorPtr*, int* resolution_out) {
  if (!resolution_out)
    return false;

  *resolution_out = resolution_;
  return true;
}

bool SaneDeviceFake::SetScanResolution(brillo::ErrorPtr*, int resolution) {
  resolution_ = resolution;
  return true;
}

bool SaneDeviceFake::GetDocumentSource(brillo::ErrorPtr*,
                                       std::string* source_name_out) {
  if (!source_name_out)
    return false;

  *source_name_out = source_name_;
  return true;
}

bool SaneDeviceFake::SetDocumentSource(brillo::ErrorPtr*,
                                       const std::string& source_name) {
  source_name_ = source_name;
  return true;
}

bool SaneDeviceFake::SetColorMode(brillo::ErrorPtr*, ColorMode) {
  return true;
}

bool SaneDeviceFake::SetScanRegion(brillo::ErrorPtr* error, const ScanRegion&) {
  return true;
}

SANE_Status SaneDeviceFake::StartScan(brillo::ErrorPtr* error) {
  // Don't allow starting the next page of the scan if we haven't completed the
  // previous one.
  if (scan_running_ && current_page_ < scan_data_.size() &&
      scan_data_offset_ < scan_data_[current_page_].size()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan is already running");
    return SANE_STATUS_DEVICE_BUSY;
  }

  if (start_scan_result_ != SANE_STATUS_GOOD) {
    return start_scan_result_;
  }

  if (scan_running_ && current_page_ + 1 == scan_data_.size()) {
    // No more scan data left.
    return SANE_STATUS_NO_DOCS;
  } else if (scan_running_) {
    current_page_++;
    scan_data_offset_ = 0;
  } else {
    scan_running_ = true;
    current_page_ = 0;
    scan_data_offset_ = 0;
  }

  return SANE_STATUS_GOOD;
}

bool SaneDeviceFake::GetScanParameters(brillo::ErrorPtr* error,
                                       ScanParameters* parameters) {
  if (!parameters || !params_.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "No parameters");
    return false;
  }

  *parameters = params_.value();
  return true;
}

SANE_Status SaneDeviceFake::ReadScanData(brillo::ErrorPtr* error,
                                         uint8_t* buf,
                                         size_t count,
                                         size_t* read_out) {
  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Scan not running");
    return SANE_STATUS_INVAL;
  }

  if (read_scan_data_result_ != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, kDbusDomain, kManagerServiceError,
                         "Reading data failed");
    return read_scan_data_result_;
  }

  if (current_page_ >= scan_data_.size()) {
    scan_running_ = false;
    return SANE_STATUS_NO_DOCS;
  }

  const std::vector<uint8_t>& page = scan_data_[current_page_];
  if (scan_data_offset_ >= page.size()) {
    *read_out = 0;
    return SANE_STATUS_EOF;
  }

  size_t to_copy = std::min(count, page.size() - scan_data_offset_);
  memcpy(buf, page.data() + scan_data_offset_, to_copy);
  *read_out = to_copy;

  scan_data_offset_ += to_copy;
  return SANE_STATUS_GOOD;
}

void SaneDeviceFake::SetValidOptionValues(
    const base::Optional<ValidOptionValues>& values) {
  values_ = values;
}

void SaneDeviceFake::SetStartScanResult(SANE_Status status) {
  start_scan_result_ = status;
}

void SaneDeviceFake::SetScanParameters(
    const base::Optional<ScanParameters>& params) {
  params_ = params;
}

void SaneDeviceFake::SetReadScanDataResult(SANE_Status result) {
  read_scan_data_result_ = result;
}

void SaneDeviceFake::SetScanData(
    const std::vector<std::vector<uint8_t>>& scan_data) {
  scan_data_ = scan_data;
}

}  // namespace lorgnette
