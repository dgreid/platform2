// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/thermal/thermal_device.h"

#include <string>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "power_manager/powerd/system/thermal/device_thermal_state.h"

namespace power_manager {
namespace system {

namespace {

// Default interval for polling the thermal device.
const int kDefaultPollIntervalMs = 5000;
const int kNumErrorBeforeGivingUp = 5;

}  // namespace

ThermalDevice::ThermalDevice() : ThermalDevice(base::FilePath()) {}

ThermalDevice::ThermalDevice(base::FilePath device_path)
    : device_path_(device_path),
      num_init_attempts_(0),
      num_read_errors_(0),
      type_(ThermalDeviceType::kUnknown),
      poll_interval_ms_(kDefaultPollIntervalMs),
      current_state_(DeviceThermalState::kUnknown) {}

ThermalDevice::~ThermalDevice() {}

void ThermalDevice::AddObserver(ThermalDeviceObserver* observer) {
  DCHECK(observer);
  observers_.AddObserver(observer);
}

void ThermalDevice::RemoveObserver(ThermalDeviceObserver* observer) {
  DCHECK(observer);
  observers_.RemoveObserver(observer);
}

DeviceThermalState ThermalDevice::GetThermalState() const {
  return current_state_;
}

void ThermalDevice::Init(bool read_immediately) {
  DCHECK(base::PathExists(device_path_));
  if (read_immediately)
    ReadDeviceState();
  StartTimer();
}

void ThermalDevice::StartTimer() {
  poll_timer_.Start(FROM_HERE,
                    base::TimeDelta::FromMilliseconds(poll_interval_ms_), this,
                    &ThermalDevice::ReadDeviceState);
}

void ThermalDevice::ReadDeviceState() {
  if (!polling_file_.HasOpenedFile() && !InitSysfsFile()) {
    if (num_init_attempts_++ >= kNumErrorBeforeGivingUp) {
      LOG(ERROR) << "Giving up on thermal device: " << device_path_;
      poll_timer_.Stop();
    }
    return;
  }

  // The timer will be restarted after the read finishes.
  poll_timer_.Stop();
  polling_file_.StartRead(
      base::Bind(&ThermalDevice::ReadCallback, base::Unretained(this)),
      base::Bind(&ThermalDevice::ErrorCallback, base::Unretained(this)));
}

void ThermalDevice::ReadCallback(const std::string& data) {
  std::string trimmed_data;
  int value;
  base::TrimWhitespaceASCII(data, base::TRIM_ALL, &trimmed_data);
  DeviceThermalState new_state = DeviceThermalState::kUnknown;
  if (base::StringToInt(trimmed_data, &value)) {
    new_state = CalculateThermalState(value);
  } else {
    LOG(ERROR) << "Could not read int value from file contents: ["
               << trimmed_data << "]";
  }
  UpdateThermalState(new_state);
  num_read_errors_ = 0;
  StartTimer();
}

void ThermalDevice::ErrorCallback() {
  LOG(ERROR) << "Error reading file: " << polling_path_;
  UpdateThermalState(DeviceThermalState::kUnknown);
  if (num_read_errors_++ >= kNumErrorBeforeGivingUp) {
    LOG(ERROR) << "Give up reading file: " << polling_path_;
    return;
  }
  StartTimer();
}

void ThermalDevice::UpdateThermalState(DeviceThermalState new_state) {
  if (current_state_ == new_state)
    return;
  current_state_ = new_state;
  LOG(INFO) << "UpdateThermalState device: " << device_path_
            << " new_state: " << DeviceThermalStateToString(new_state);
  for (auto& observer : observers_)
    observer.OnThermalChanged(this);
}

ThermalDeviceType ThermalDevice::GetType() const {
  return type_;
}

}  // namespace system
}  // namespace power_manager
