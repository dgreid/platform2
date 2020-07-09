// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_RESOLVER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_RESOLVER_H_

#include <list>
#include <memory>
#include <set>
#include <string>

#include <base/files/file_path.h>

#include "diagnostics/common/statusor.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Resolves the purpose of the device.
class StorageDeviceResolver {
 public:
  static StatusOr<std::unique_ptr<StorageDeviceResolver>> Create(
      const base::FilePath& rootfs);

  virtual ~StorageDeviceResolver() = default;

  virtual chromeos::cros_healthd::mojom::StorageDevicePurpose GetDevicePurpose(
      const std::string& dev_name) const;

 protected:
  StorageDeviceResolver() = default;

 private:
  static StatusOr<std::set<std::string>> GetSwapDevices(
      const base::FilePath& rootfs);
  static StatusOr<std::set<std::string>> ResolveDevices(
      const base::FilePath& rootfs, const std::list<std::string>& swap_devs);

  explicit StorageDeviceResolver(
      const std::set<std::string>& swap_backing_devices);

  const std::set<std::string> swap_backing_devices_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEVICE_RESOLVER_H_
