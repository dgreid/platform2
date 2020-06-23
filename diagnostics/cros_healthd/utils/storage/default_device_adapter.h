// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEFAULT_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEFAULT_DEVICE_ADAPTER_H_

#include <string>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"

namespace diagnostics {

// Default data retrieval behaviour. This class is used for devices which do not
// have a dedicated adapter and the responsibility of the class is to preserve
// the legacy behaviour. E.g. in the previous code, regardless of the type of
// the device 'name' and 'model' sysfs pseudo-files would have been read.
// Current implementation specializes data retrieval per device type. However,
// if a device type which doesn't have a specialized adapter yet, we want the
// data provided for it to be on par with what it used to be.
class DefaultDeviceAdapter : public StorageDeviceAdapter {
 public:
  explicit DefaultDeviceAdapter(const base::FilePath& dev_sys_path);
  DefaultDeviceAdapter(const DefaultDeviceAdapter&) = delete;
  DefaultDeviceAdapter(DefaultDeviceAdapter&&) = delete;
  DefaultDeviceAdapter& operator=(const DefaultDeviceAdapter&) = delete;
  DefaultDeviceAdapter& operator=(DefaultDeviceAdapter&&) = delete;
  ~DefaultDeviceAdapter() override = default;

  std::string GetDeviceName() const override;
  std::string GetModel() const override;

 private:
  const base::FilePath dev_sys_path_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_DEFAULT_DEVICE_ADAPTER_H_