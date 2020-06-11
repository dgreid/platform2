// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"

#include <cstdint>
#include <string>

#include <base/strings/stringprintf.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kModelFile[] = "device/model";
constexpr char kAltModelFile[] = "device/name";

}  // namespace

DefaultDeviceAdapter::DefaultDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string DefaultDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

StatusOr<mojo_ipc::BlockDeviceVendor> DefaultDeviceAdapter::GetVendorId()
    const {
  // Not supported for unknown device type, returns default 0.
  mojo_ipc::BlockDeviceVendor result;
  result.set_other(0);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceProduct> DefaultDeviceAdapter::GetProductId()
    const {
  // Not supported for unknown device type, returns default 0.
  mojo_ipc::BlockDeviceProduct result;
  result.set_other(0);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceRevision> DefaultDeviceAdapter::GetRevision()
    const {
  // Not supported for unknown device type, returns default 0.
  mojo_ipc::BlockDeviceRevision result;
  result.set_other(0);
  return result;
}

StatusOr<std::string> DefaultDeviceAdapter::GetModel() const {
  // This piece is for compatibility and will be replaced with a simple
  // return ""; when all the devices are covered properly.
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model)) {
    if (!ReadAndTrimString(dev_sys_path_, kAltModelFile, &model)) {
      return Status(
          StatusCode::kUnavailable,
          base::StringPrintf("Failed to read %s/%s and %s/%s",
                             dev_sys_path_.value().c_str(), kModelFile,
                             dev_sys_path_.value().c_str(), kAltModelFile));
    }
  }
  return model;
}

StatusOr<mojo_ipc::BlockDeviceFirmware>
DefaultDeviceAdapter::GetFirmwareVersion() const {
  // Not supported for unknown device type, returns default 0.
  mojo_ipc::BlockDeviceFirmware result;
  result.set_other(0);
  return result;
}

}  // namespace diagnostics
