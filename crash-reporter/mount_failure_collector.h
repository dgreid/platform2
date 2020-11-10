// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_MOUNT_FAILURE_COLLECTOR_H_
#define CRASH_REPORTER_MOUNT_FAILURE_COLLECTOR_H_

#include <string>

#include <base/files/file_path.h>

#include "crash-reporter/crash_collector.h"

// Block device type for collecting mount failure data from.
enum class StorageDeviceType {
  kStateful = 0,
  kEncryptedStateful,
  kInvalidDevice
};

// Collect mount failure information from a given device. At the moment, only
// the stateful and encrypted stateful partition are supported.
class MountFailureCollector : public CrashCollector {
 public:
  explicit MountFailureCollector(StorageDeviceType device_type);
  MountFailureCollector(const MountFailureCollector&) = delete;
  MountFailureCollector& operator=(const MountFailureCollector&) = delete;

  ~MountFailureCollector() override = default;

  bool Collect(bool is_mount_failure);

  static StorageDeviceType ValidateStorageDeviceType(const std::string& device);
  static std::string StorageDeviceTypeToString(StorageDeviceType device_type);

 private:
  StorageDeviceType device_type_;
};

#endif  // CRASH_REPORTER_MOUNT_FAILURE_COLLECTOR_H_
