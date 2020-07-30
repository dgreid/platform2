// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/nvme_device_adapter.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kVendorIdFile[] = "device/device/subsystem_vendor";
constexpr char kProductIdFile[] = "device/device/subsystem_device";
constexpr char kRevisionFile[] = "device/device/revision";
constexpr char kModelFile[] = "device/model";
constexpr char kFirmwareVersionFile[] = "device/firmware_rev";

constexpr uint64_t kU64Size = 8;

// Convenience wrapper for error status.
Status ReadFailure(const base::FilePath& path) {
  return Status(StatusCode::kUnavailable,
                base::StringPrintf("Failed to read %s", path.value().c_str()));
}

}  // namespace

NvmeDeviceAdapter::NvmeDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string NvmeDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

StatusOr<mojo_ipc::BlockDeviceVendor> NvmeDeviceAdapter::GetVendorId() const {
  uint32_t value;
  if (!ReadInteger(dev_sys_path_, kVendorIdFile, &base::HexStringToUInt,
                   &value)) {
    return ReadFailure(dev_sys_path_.Append(kVendorIdFile));
  }

  mojo_ipc::BlockDeviceVendor result;
  result.set_nvme_subsystem_vendor(value);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceProduct> NvmeDeviceAdapter::GetProductId() const {
  uint64_t value;
  if (!ReadInteger(dev_sys_path_, kProductIdFile, &base::HexStringToUInt64,
                   &value)) {
    return ReadFailure(dev_sys_path_.Append(kProductIdFile));
  }

  mojo_ipc::BlockDeviceProduct result;
  result.set_nvme_subsystem_device(value);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceRevision> NvmeDeviceAdapter::GetRevision() const {
  uint32_t value;
  if (!ReadInteger(dev_sys_path_, kRevisionFile, &base::HexStringToUInt,
                   &value))
    return ReadFailure(dev_sys_path_.Append(kRevisionFile));

  mojo_ipc::BlockDeviceRevision result;
  result.set_nvme_pcie_rev(value);
  return result;
}

StatusOr<std::string> NvmeDeviceAdapter::GetModel() const {
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model))
    return ReadFailure(dev_sys_path_.Append(kModelFile));

  return model;
}

StatusOr<mojo_ipc::BlockDeviceFirmware> NvmeDeviceAdapter::GetFirmwareVersion()
    const {
  std::string str_value;
  auto path = dev_sys_path_.Append(kFirmwareVersionFile);
  if (!ReadFileToString(path, &str_value))
    return ReadFailure(path);

  char bytes[kU64Size] = {0};
  memcpy(bytes, str_value.c_str(), std::min(str_value.length(), kU64Size));
  uint64_t value = *reinterpret_cast<uint64_t*>(bytes);

  mojo_ipc::BlockDeviceFirmware result;
  result.set_nvme_firmware_rev(value);
  return result;
}

}  // namespace diagnostics
