// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/nvme_device_adapter.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

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
constexpr char kConfigFile[] = "device/device/config";
constexpr char kModelFile[] = "device/model";
constexpr char kFirmwareVersionFile[] = "device/firmware_rev";

constexpr size_t kU64Size = 8;

// Extract from PCI local bus spec 2.2 from December 18, 1998
// (page 191, figure 6-1)
struct pci_config_space {
  uint16_t notrequired[4];
  uint8_t revision;
  char rest[0];
} __attribute__((packed));

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

StatusOr<mojo_ipc::BlockDeviceRevision>
NvmeDeviceAdapter::GetRevisionOnPre410Kernel() const {
  mojo_ipc::BlockDeviceRevision result;
  std::vector<char> bytes;
  bytes.resize(sizeof(pci_config_space));

  int read = base::ReadFile(dev_sys_path_.Append(kConfigFile), bytes.data(),
                            bytes.size());

  // Failed to read the file.
  if (read < 0)
    return ReadFailure(dev_sys_path_.Append(kConfigFile));

  // File present, but the config space is truncated, assume revision == 0.
  if (read < sizeof(pci_config_space)) {
    result.set_nvme_pcie_rev(0);
    return result;
  }

  pci_config_space* pci = reinterpret_cast<pci_config_space*>(bytes.data());
  result.set_nvme_pcie_rev(pci->revision);
  return result;
}

StatusOr<mojo_ipc::BlockDeviceRevision> NvmeDeviceAdapter::GetRevision() const {
  uint32_t value;

  // Try legacy method if the revision file is missing.
  if (!base::PathExists(dev_sys_path_.Append(kRevisionFile)))
    return GetRevisionOnPre410Kernel();

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
