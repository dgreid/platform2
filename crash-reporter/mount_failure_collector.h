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
  ~MountFailureCollector() override = default;

  void Initialize(IsFeedbackAllowedFunction is_feedback_allowed, bool early);

  bool Collect();

  static StorageDeviceType ValidateStorageDeviceType(const std::string& device);
  static std::string StorageDeviceTypeToString(StorageDeviceType device_type);

 private:
  StorageDeviceType device_type_;
  DISALLOW_COPY_AND_ASSIGN(MountFailureCollector);
};

#endif  // CRASH_REPORTER_MOUNT_FAILURE_COLLECTOR_H_
