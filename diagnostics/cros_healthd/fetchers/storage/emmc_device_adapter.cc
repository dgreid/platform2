// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/emmc_device_adapter.h"

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

constexpr char kOemIdFile[] = "device/oemid";
constexpr char kPnmIdFile[] = "device/name";
constexpr char kRevisionFile[] = "device/rev";
constexpr char kAltRevisionFile[] = "device/hwrev";
constexpr char kModelFile[] = "device/name";
constexpr char kFirmwareVersionFile[] = "device/fwrev";

constexpr size_t kU64Size = 8;

// Convenience wrapper for error status.
Status ReadFailure(const base::FilePath& path) {
  return Status(StatusCode::kUnavailable,
                base::StringPrintf("Failed to read %s", path.value().c_str()));
}

}  // namespace

EmmcDeviceAdapter::EmmcDeviceAdapter(const base::FilePath& dev_sys_path)
    : dev_sys_path_(dev_sys_path) {}

std::string EmmcDeviceAdapter::GetDeviceName() const {
  return dev_sys_path_.BaseName().value();
}

StatusOr<mojo_ipc::BlockDeviceVendor> EmmcDeviceAdapter::GetVendorId() const {
  uint32_t value;
  if (!ReadInteger(dev_sys_path_, kOemIdFile, &base::HexStringToUInt, &value)) {
    return ReadFailure(dev_sys_path_.Append(kOemIdFile));
  }

  mojo_ipc::BlockDeviceVendor result;
  result.set_emmc_oemid(value);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceProduct> EmmcDeviceAdapter::GetProductId() const {
  std::string str_value;
  auto path = dev_sys_path_.Append(kPnmIdFile);
  if (!ReadFileToString(path, &str_value))
    return ReadFailure(dev_sys_path_.Append(kPnmIdFile));

  char bytes[kU64Size] = {0};
  memcpy(bytes, str_value.c_str(), std::min(str_value.length(), kU64Size));
  uint64_t value = *reinterpret_cast<uint64_t*>(bytes);

  mojo_ipc::BlockDeviceProduct result;
  result.set_emmc_pnm(value);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceRevision> EmmcDeviceAdapter::GetRevision() const {
  uint32_t value;
  if (!ReadInteger(dev_sys_path_, kRevisionFile, &base::HexStringToUInt,
                   &value)) {
    // Older eMMC devices may not have prv, but they should have hwrev.
    if (!ReadInteger(dev_sys_path_, kAltRevisionFile, &base::HexStringToUInt,
                     &value)) {
      return Status(
          StatusCode::kUnavailable,
          base::StringPrintf("Failed to read %s/%s and %s/%s",
                             dev_sys_path_.value().c_str(), kRevisionFile,
                             dev_sys_path_.value().c_str(), kAltRevisionFile));
    }
  }

  mojo_ipc::BlockDeviceRevision result;
  result.set_emmc_prv(value);
  return result;
}

StatusOr<std::string> EmmcDeviceAdapter::GetModel() const {
  std::string model;
  if (!ReadAndTrimString(dev_sys_path_, kModelFile, &model))
    return ReadFailure(dev_sys_path_.Append(kModelFile));
  return model;
}

StatusOr<mojo_ipc::BlockDeviceFirmware> EmmcDeviceAdapter::GetFirmwareVersion()
    const {
  uint64_t value;
  if (!ReadInteger(dev_sys_path_, kFirmwareVersionFile,
                   &base::HexStringToUInt64, &value)) {
    return ReadFailure(dev_sys_path_.Append(kFirmwareVersionFile));
  }

  mojo_ipc::BlockDeviceFirmware result;
  result.set_emmc_fwrev(value);
  return result;
}

}  // namespace diagnostics
