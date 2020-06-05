// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_

#include <string>

#include <base/files/file_path.h>

namespace diagnostics {

// StorageDeviceInfo encapsulates the logic for retrieving info about an
// individual storage device.
class StorageDeviceInfo {
 public:
  StorageDeviceInfo(const base::FilePath& dev_sys_path,
                    const base::FilePath& dev_node_path,
                    const std::string& subsystem);
  StorageDeviceInfo(const StorageDeviceInfo&) = delete;
  StorageDeviceInfo(StorageDeviceInfo&&) = delete;
  StorageDeviceInfo& operator=(const StorageDeviceInfo&) = delete;
  StorageDeviceInfo& operator=(StorageDeviceInfo&&) = delete;

  base::FilePath GetSysPath() const;
  base::FilePath GetDevNodePath() const;
  std::string GetSubsystem() const;

 private:
  const base::FilePath dev_sys_path_;
  const base::FilePath dev_node_path_;
  const std::string subsystem_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_INFO_H_
