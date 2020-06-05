// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/device_info.h"

#include <string>

#include <base/files/file_path.h>

namespace diagnostics {

StorageDeviceInfo::StorageDeviceInfo(const base::FilePath& dev_sys_path,
                                     const base::FilePath& dev_node_path,
                                     const std::string& subsystem)
    : dev_sys_path_(dev_sys_path),
      dev_node_path_(dev_node_path),
      subsystem_(subsystem) {}

base::FilePath StorageDeviceInfo::GetSysPath() const {
  return dev_sys_path_;
}

base::FilePath StorageDeviceInfo::GetDevNodePath() const {
  return dev_node_path_;
}

std::string StorageDeviceInfo::GetSubsystem() const {
  return subsystem_;
}

}  // namespace diagnostics
