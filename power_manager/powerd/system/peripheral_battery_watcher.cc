// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/peripheral_battery_watcher.h"

#include <fcntl.h>

#include <cerrno>
#include <string>

#include <base/bind.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "power_manager/common/util.h"
#include "power_manager/powerd/system/dbus_wrapper.h"
#include "power_manager/proto_bindings/peripheral_battery_status.pb.h"

namespace power_manager {
namespace system {

namespace {

// Default path examined for peripheral battery directories.
const char kDefaultPeripheralBatteryPath[] = "/sys/class/power_supply/";

// Default interval for polling the device battery info.
const int kDefaultPollIntervalMs = 600000;

// Reads |path| to |value_out| and trims trailing whitespace. False is returned
// if the file doesn't exist or can't be read.
bool ReadStringFromFile(const base::FilePath& path, std::string* value_out) {
  if (!base::ReadFileToString(path, value_out))
    return false;

  base::TrimWhitespaceASCII(*value_out, base::TRIM_TRAILING, value_out);
  return true;
}

}  // namespace

const char PeripheralBatteryWatcher::kScopeFile[] = "scope";
const char PeripheralBatteryWatcher::kScopeValueDevice[] = "Device";
const char PeripheralBatteryWatcher::kStatusFile[] = "status";
const char PeripheralBatteryWatcher::kStatusValueUnknown[] = "Unknown";
const char PeripheralBatteryWatcher::kModelNameFile[] = "model_name";
const char PeripheralBatteryWatcher::kCapacityFile[] = "capacity";

PeripheralBatteryWatcher::PeripheralBatteryWatcher()
    : dbus_wrapper_(nullptr),
      peripheral_battery_path_(kDefaultPeripheralBatteryPath),
      poll_interval_ms_(kDefaultPollIntervalMs) {}

PeripheralBatteryWatcher::~PeripheralBatteryWatcher() {}

void PeripheralBatteryWatcher::Init(DBusWrapperInterface* dbus_wrapper) {
  dbus_wrapper_ = dbus_wrapper;
  ReadBatteryStatuses();
}

bool PeripheralBatteryWatcher::IsPeripheralDevice(
    const base::FilePath& device_path) const {
  // Peripheral batteries have device scopes.
  std::string scope;
  return (ReadStringFromFile(device_path.Append(kScopeFile), &scope) &&
          scope == kScopeValueDevice);
}

void PeripheralBatteryWatcher::GetBatteryList(
    std::vector<base::FilePath>* battery_list) {
  battery_list->clear();
  base::FileEnumerator dir_enumerator(peripheral_battery_path_, false,
                                      base::FileEnumerator::DIRECTORIES);

  for (base::FilePath device_path = dir_enumerator.Next(); !device_path.empty();
       device_path = dir_enumerator.Next()) {
    if (!IsPeripheralDevice(device_path))
      continue;

    // Some devices may initially have an unknown status; avoid reporting
    // them: http://b/64392016
    std::string status;
    if (ReadStringFromFile(device_path.Append(kStatusFile), &status) &&
        status == kStatusValueUnknown)
      continue;

    battery_list->push_back(device_path);
  }
}

void PeripheralBatteryWatcher::ReadBatteryStatus(const base::FilePath& path) {
  // sysfs entry "capacity" has the current battery level.
  base::FilePath capacity_path = path.Append(kCapacityFile);
  if (!base::PathExists(capacity_path))
    return;

  std::string model_name;
  if (!ReadStringFromFile(path.Append(kModelNameFile), &model_name))
    return;

  battery_readers_.push_back(std::make_unique<AsyncFileReader>());
  AsyncFileReader* reader = battery_readers_.back().get();

  if (reader->Init(capacity_path)) {
    reader->StartRead(base::Bind(&PeripheralBatteryWatcher::ReadCallback,
                                 base::Unretained(this), path, model_name),
                      base::Bind(&PeripheralBatteryWatcher::ErrorCallback,
                                 base::Unretained(this), path, model_name));
  } else {
    LOG(ERROR) << "Can't read battery capacity " << capacity_path.value();
  }
}

void PeripheralBatteryWatcher::ReadBatteryStatuses() {
  battery_readers_.clear();

  std::vector<base::FilePath> new_battery_list;
  GetBatteryList(&new_battery_list);

  for (const base::FilePath& path : new_battery_list) {
    ReadBatteryStatus(path);
  }

  poll_timer_.Start(FROM_HERE,
                    base::TimeDelta::FromMilliseconds(poll_interval_ms_), this,
                    &PeripheralBatteryWatcher::ReadBatteryStatuses);
}

void PeripheralBatteryWatcher::SendBatteryStatus(const base::FilePath& path,
                                                 const std::string& model_name,
                                                 int level) {
  PeripheralBatteryStatus proto;
  proto.set_path(path.value());
  proto.set_name(model_name);
  if (level >= 0)
    proto.set_level(level);
  dbus_wrapper_->EmitSignalWithProtocolBuffer(kPeripheralBatteryStatusSignal,
                                              proto);
}

void PeripheralBatteryWatcher::ReadCallback(const base::FilePath& path,
                                            const std::string& model_name,
                                            const std::string& data) {
  std::string trimmed_data;
  base::TrimWhitespaceASCII(data, base::TRIM_ALL, &trimmed_data);
  int level = -1;
  if (base::StringToInt(trimmed_data, &level)) {
    SendBatteryStatus(path, model_name, level);
  } else {
    LOG(ERROR) << "Invalid battery level reading : [" << data << "]"
               << " from " << path.value();
  }
}

void PeripheralBatteryWatcher::ErrorCallback(const base::FilePath& path,
                                             const std::string& model_name) {
  SendBatteryStatus(path, model_name, -1);
}

}  // namespace system
}  // namespace power_manager
