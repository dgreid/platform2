// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STORAGE_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STORAGE_DEVICE_ADAPTER_H_

#include <string>
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

enum AdapterError { kFileError = 1, kParseError };

// StorageDeviceAdapter is an accessor interface to the subsystem-specific
// information in a uniform way.
class StorageDeviceAdapter {
 public:
  virtual ~StorageDeviceAdapter() = default;

  virtual std::string GetDeviceName() const = 0;
  virtual StatusOr<std::string> GetModel() const = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STORAGE_DEVICE_ADAPTER_H_
