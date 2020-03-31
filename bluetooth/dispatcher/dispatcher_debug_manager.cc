// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/dispatcher_debug_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>

namespace bluetooth {

namespace {

constexpr char kBluetoothDebugObjectPath[] = "/org/chromium/Bluetooth";
constexpr char kDebugConfigFile[] = "/var/lib/bluetooth/debug.conf";
constexpr uint8_t kDefaultVerbosityLevel = 0;
constexpr uint8_t kDispatcherMinimumVerbosityLevel = 0;

constexpr const char* kDebugProperties[] = {
    bluetooth_debug::kDispatcherLevelProperty,
    bluetooth_debug::kNewblueLevelProperty,
    bluetooth_debug::kBluezLevelProperty,
    bluetooth_debug::kKernelLevelProperty,
};

}  // namespace

DispatcherDebugManager::DispatcherDebugManager(
    scoped_refptr<dbus::Bus> bus,
    ExportedObjectManagerWrapper* exported_object_manager_wrapper)
    : bus_(bus),
      exported_object_manager_wrapper_(exported_object_manager_wrapper),
      weak_ptr_factory_(this) {
  CHECK(exported_object_manager_wrapper_ != nullptr);
}

void DispatcherDebugManager::Init() {
  dbus::ObjectPath object_path(kBluetoothDebugObjectPath);

  // Initialize D-Bus proxies.
  exported_object_manager_wrapper_->AddExportedInterface(
      object_path, bluetooth_debug::kBluetoothDebugInterface,
      base::Bind(&ExportedObjectManagerWrapper::SetupStandardPropertyHandlers));

  debug_interface_ = exported_object_manager_wrapper_->GetExportedInterface(
      object_path, bluetooth_debug::kBluetoothDebugInterface);

  RegisterProperties();
  uint8_t initial_log_level = debug_interface_
                                  ->EnsureExportedPropertyRegistered<uint8_t>(
                                      bluetooth_debug::kDispatcherLevelProperty)
                                  ->value();
  SetDispatcherLogLevel(initial_log_level);

  debug_interface_->AddSimpleMethodHandlerWithErrorAndMessage(
      bluetooth_debug::kSetLevels, base::Unretained(this),
      &DispatcherDebugManager::HandleSetLevels);

  debug_interface_->ExportAndBlock();
}

void DispatcherDebugManager::RegisterProperties() {
  std::vector<uint8_t> prop_values;
  int expected_num_of_props = base::size(kDebugProperties);

  if (!ParseConfigFile(expected_num_of_props, &prop_values))
    prop_values.assign(expected_num_of_props, kDefaultVerbosityLevel);

  for (int i = 0; i < expected_num_of_props; i++) {
    debug_interface_
        ->EnsureExportedPropertyRegistered<uint8_t>(kDebugProperties[i])
        ->SetValue(prop_values[i]);
  }
}

bool DispatcherDebugManager::ParseConfigFile(int expected_num_of_values,
                                             std::vector<uint8_t>* values) {
  base::FilePath conf_path(kDebugConfigFile);
  std::string file_content;
  if (!base::PathExists(conf_path))
    return false;
  if (!base::ReadFileToString(conf_path, &file_content)) {
    LOG(ERROR) << "Cannot read debug verbosity from " << conf_path.value();
    return false;
  }

  std::vector<std::string> values_str = base::SplitString(
      file_content, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (values_str.size() != expected_num_of_values) {
    LOG(ERROR) << "Different number of parameters in " << conf_path.value();
    return false;
  }

  for (const auto& value_str : values_str) {
    unsigned int value;
    if (base::StringToUint(value_str, &value)) {
      values->push_back(value);
    } else {
      LOG(ERROR) << "Parsing failed " << conf_path.value();
      values_str.clear();
      return false;
    }
  }

  return true;
}

bool DispatcherDebugManager::HandleSetLevels(brillo::ErrorPtr* error,
                                             dbus::Message* message,
                                             uint8_t dispatcher_level,
                                             uint8_t newblue_level,
                                             uint8_t bluez_level,
                                             uint8_t kernel_level) {
  VLOG(2) << "Sender=" << message->GetSender() << " set debug level:"
          << " dispatcher:" << static_cast<int>(dispatcher_level)
          << ", newblue:" << static_cast<int>(newblue_level)
          << ", bluez:" << static_cast<int>(bluez_level)
          << ", kernel:" << static_cast<int>(kernel_level);

  std::string file_content;
  file_content.append(std::to_string(dispatcher_level))
      .append("\n")
      .append(std::to_string(newblue_level))
      .append("\n")
      .append(std::to_string(bluez_level))
      .append("\n")
      .append(std::to_string(kernel_level));

  base::FilePath conf_path(kDebugConfigFile);
  if (!base::ImportantFileWriter::WriteFileAtomically(conf_path, file_content))
    LOG(ERROR) << "Cannot write debug verbosity to " << conf_path.value();

  uint8_t property_levels[] = {dispatcher_level, newblue_level, bluez_level,
                               kernel_level};
  for (int i = 0; i < base::size(kDebugProperties); i++) {
    debug_interface_
        ->EnsureExportedPropertyRegistered<uint8_t>(kDebugProperties[i])
        ->SetValue(property_levels[i]);
  }

  SetDispatcherLogLevel(dispatcher_level);
  return true;
}

void DispatcherDebugManager::SetDispatcherLogLevel(int verbosity) {
  if (verbosity < kDispatcherMinimumVerbosityLevel) {
    LOG(WARNING) << "Invalid verbosity level for dispatcher";
    return;
  }

  if (current_verbosity_ == verbosity)
    return;

  current_verbosity_ = verbosity;
  LOG(INFO) << "Log level is set to " << verbosity;
  logging::SetMinLogLevel(-verbosity);
}

}  // namespace bluetooth
