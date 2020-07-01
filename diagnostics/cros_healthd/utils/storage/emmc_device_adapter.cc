// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/emmc_device_adapter.h"

#include <string>

#include <base/strings/stringprintf.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

namespace {

constexpr char kModelFile[] = "device/name";

}

EmmcDeviceAdapter::EmmcDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string EmmcDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

StatusOr<std::string> EmmcDeviceAdapter::GetModel() const {
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model)) {
    return Status(
        StatusCode::kUnavailable,
        base::StringPrintf("Failed to read %s/%s",
                           dev_sys_path_.value().c_str(), kModelFile));
  }
  return model;
}

}  // namespace diagnostics
