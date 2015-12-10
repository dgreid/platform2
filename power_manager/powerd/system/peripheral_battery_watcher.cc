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

#include "power_manager/common/dbus_sender.h"
#include "power_manager/common/util.h"
#include "power_manager/proto_bindings/peripheral_battery_status.pb.h"

namespace power_manager {
namespace system {

namespace {

// Default path examined for peripheral battery directories.
const base::FilePath::CharType kDefaultPeripheralBatteryPath[] =
    FILE_PATH_LITERAL("/sys/class/power_supply/");

// Default interval for polling the device battery info.
const int kDefaultPollIntervalMs = 600000;

}  // namespace

PeripheralBatteryWatcher::PeripheralBatteryWatcher()
    : dbus_sender_(NULL),
      peripheral_battery_path_(kDefaultPeripheralBatteryPath),
      poll_interval_ms_(kDefaultPollIntervalMs) {
}

PeripheralBatteryWatcher::~PeripheralBatteryWatcher() {}

void PeripheralBatteryWatcher::Init(DBusSenderInterface* dbus_sender) {
  dbus_sender_ = dbus_sender;
  ReadBatteryStatuses();
}

void PeripheralBatteryWatcher::GetBatteryList(
    std::vector<base::FilePath>* battery_list) {
  battery_list->clear();
  base::FileEnumerator dir_enumerator(
      peripheral_battery_path_, false, base::FileEnumerator::DIRECTORIES);

  // Peripheral battery has a sysfs entry with name "scope" containing value
  // "Device".
  for (base::FilePath check_path = dir_enumerator.Next(); !check_path.empty();
       check_path = dir_enumerator.Next()) {
    base::FilePath scope_path = check_path.Append("scope");
    if (!base::PathExists(scope_path))
      continue;

    std::string buf;
    base::ReadFileToString(scope_path, &buf);
    base::TrimWhitespaceASCII(buf, base::TRIM_TRAILING, &buf);
    if (buf != "Device")
      continue;

    battery_list->push_back(check_path);
  }
}

void PeripheralBatteryWatcher::ReadBatteryStatuses() {
  battery_readers_.clear();

  std::vector<base::FilePath> new_battery_list;
  GetBatteryList(&new_battery_list);

  for (size_t i = 0; i < new_battery_list.size(); i++) {
    base::FilePath path = new_battery_list[i];

    // sysfs entry "capacity" has the current battery level.
    base::FilePath capacity_path = path.Append("capacity");
    if (!base::PathExists(capacity_path))
      continue;

    base::FilePath model_name_path = path.Append("model_name");
    if (!base::PathExists(model_name_path))
      continue;

    std::string model_name;
    base::ReadFileToString(model_name_path, &model_name);
    base::TrimWhitespaceASCII(model_name, base::TRIM_TRAILING, &model_name);

    AsyncFileReader* reader = new AsyncFileReader;
    battery_readers_.push_back(reader);

    if (reader->Init(capacity_path.value())) {
      reader->StartRead(
          base::Bind(&PeripheralBatteryWatcher::ReadCallback,
                     base::Unretained(this),
                     path.value(), model_name),
          base::Bind(&PeripheralBatteryWatcher::ErrorCallback,
                     base::Unretained(this),
                     path.value(), model_name));
    } else {
      LOG(ERROR) << "Can't read battery capacity " << capacity_path.value();
    }
  }
  poll_timer_.Start(FROM_HERE,
      base::TimeDelta::FromMilliseconds(poll_interval_ms_),
      this, &PeripheralBatteryWatcher::ReadBatteryStatuses);
}

void PeripheralBatteryWatcher::SendBatteryStatus(const std::string& path,
                                                 const std::string& model_name,
                                                 int level) {
  PeripheralBatteryStatus proto;
  proto.set_path(path);
  proto.set_name(model_name);
  if (level >= 0)
    proto.set_level(level);
  dbus_sender_->EmitSignalWithProtocolBuffer(kPeripheralBatteryStatusSignal,
                                             proto);
}

void PeripheralBatteryWatcher::ReadCallback(const std::string& path,
                                            const std::string& model_name,
                                            const std::string& data) {
  std::string trimmed_data;
  base::TrimWhitespaceASCII(data, base::TRIM_ALL, &trimmed_data);
  int level = -1;
  if (base::StringToInt(trimmed_data, &level)) {
    SendBatteryStatus(path, model_name, level);
  } else {
    LOG(ERROR) << "Invalid battery level reading : [" << data << "]"
               << " from " << path;
  }
}

void PeripheralBatteryWatcher::ErrorCallback(const std::string& path,
                                             const std::string& model_name) {
  SendBatteryStatus(path, model_name, -1);
}

}  // namespace system
}  // namespace power_manager
