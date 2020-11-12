// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/mount_failure_collector.h"

#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>

#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

namespace {
const char kEncryptedStatefulDeviceLabel[] = "encstateful";
const char kStatefulDeviceLabel[] = "stateful";
const char kInvalidDeviceLabel[] = "invalid";

std::vector<std::string> ConstructLoggingCommands(StorageDeviceType device_type,
                                                  bool is_mount_failure) {
  std::vector<std::string> cmds;
  if (is_mount_failure) {
    // Common logging for mount failure cases:
    // - dumpe2fs.
    // - dmesg for current run.
    // - ramoops, if any.
    cmds = {"dumpe2fs_" +
                MountFailureCollector::StorageDeviceTypeToString(device_type),
            "kernel-warning", "console-ramoops"};

    // For encrypted stateful mount failure, add logs from mount-encrypted.
    if (device_type == StorageDeviceType::kEncryptedStateful)
      cmds.push_back("mount-encrypted");
  } else {
    cmds = {"shutdown_umount_failure_state", "umount-encrypted"};
  }
  return cmds;
}

}  // namespace

MountFailureCollector::MountFailureCollector(StorageDeviceType device_type)
    : CrashCollector("mount_failure_collector"), device_type_(device_type) {}

// static
StorageDeviceType MountFailureCollector::ValidateStorageDeviceType(
    const std::string& device_label) {
  if (device_label == kStatefulDeviceLabel)
    return StorageDeviceType::kStateful;
  else if (device_label == kEncryptedStatefulDeviceLabel)
    return StorageDeviceType::kEncryptedStateful;
  else
    return StorageDeviceType::kInvalidDevice;
}

// static
std::string MountFailureCollector::StorageDeviceTypeToString(
    StorageDeviceType device_type) {
  switch (device_type) {
    case StorageDeviceType::kStateful:
      return kStatefulDeviceLabel;
    case StorageDeviceType::kEncryptedStateful:
      return kEncryptedStatefulDeviceLabel;
    default:
      return kInvalidDeviceLabel;
  }
}

bool MountFailureCollector::Collect(bool is_mount_failure) {
  if (device_type_ == StorageDeviceType::kInvalidDevice) {
    LOG(ERROR) << "Invalid storage device.";
    return true;
  }

  std::string device_label = StorageDeviceTypeToString(device_type_);
  std::string exec_name = (is_mount_failure ? "mount" : "umount");
  exec_name += "_failure_" + device_label;
  std::string dump_basename = FormatDumpBasename(exec_name, time(nullptr), 0);

  auto logging_cmds = ConstructLoggingCommands(device_type_, is_mount_failure);

  base::FilePath crash_directory;
  if (!GetCreatedCrashDirectoryByEuid(kRootUid, &crash_directory, nullptr)) {
    return true;
  }

  // Use exec name as the crash signature.
  AddCrashMetaData("sig", exec_name);

  base::FilePath log_path = GetCrashPath(crash_directory, dump_basename, "log");
  base::FilePath meta_path =
      GetCrashPath(crash_directory, dump_basename, "meta");

  bool result =
      GetMultipleLogContents(log_config_path_, logging_cmds, log_path);
  if (result) {
    FinishCrash(meta_path, exec_name, log_path.BaseName().value());
  }

  return true;
}
