// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/device_info.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_split.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/disk_iostat.h"
#include "diagnostics/cros_healthd/utils/storage/emmc_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/nvme_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

std::unique_ptr<StorageDeviceAdapter> CreateAdapter(
    const base::FilePath& dev_sys_path, const std::string& subsystem) {
  // A particular device has a chain of subsystems it belongs to. We pass them
  // here in a colon-separated format (e.g. "block:mmc:mmc_host:pci"). We expect
  // that the root subsystem is "block", and the type of the block device
  // immediately follows it.
  constexpr char kBlockSubsystem[] = "block";
  constexpr char kNvmeSubsystem[] = "nvme";
  constexpr char kMmcSubsystem[] = "mmc";
  constexpr int kBlockSubsystemIndex = 0;
  constexpr int kBlockTypeSubsystemIndex = 1;
  constexpr int kMinComponentLength = 2;
  auto subs = base::SplitString(subsystem, ":", base::KEEP_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY);

  if (subs.size() < kMinComponentLength ||
      subs[kBlockSubsystemIndex] != kBlockSubsystem)
    return nullptr;
  if (subs[kBlockTypeSubsystemIndex] == kNvmeSubsystem)
    return std::make_unique<NvmeDeviceAdapter>(dev_sys_path);
  if (subs[kBlockTypeSubsystemIndex] == kMmcSubsystem)
    return std::make_unique<EmmcDeviceAdapter>(dev_sys_path);
  return std::make_unique<DefaultDeviceAdapter>(dev_sys_path);
}

}  // namespace

StorageDeviceInfo::StorageDeviceInfo(const base::FilePath& dev_sys_path,
                                     const base::FilePath& dev_node_path,
                                     const std::string& subsystem,
                                     std::unique_ptr<Platform> platform)
    : dev_sys_path_(dev_sys_path),
      dev_node_path_(dev_node_path),
      subsystem_(subsystem),
      adapter_(CreateAdapter(dev_sys_path, subsystem)),
      platform_(std::move(platform)),
      iostat_(dev_sys_path) {
  DCHECK(adapter_);
  DCHECK(platform_);
}

base::FilePath StorageDeviceInfo::GetSysPath() const {
  return dev_sys_path_;
}

base::FilePath StorageDeviceInfo::GetDevNodePath() const {
  return dev_node_path_;
}

std::string StorageDeviceInfo::GetSubsystem() const {
  return subsystem_;
}

StatusOr<uint64_t> StorageDeviceInfo::GetSizeBytes() {
  return platform_->GetDeviceSizeBytes(dev_node_path_);
}

StatusOr<uint64_t> StorageDeviceInfo::GetBlockSizeBytes() {
  return platform_->GetDeviceBlockSizeBytes(dev_node_path_);
}

DiskIoStat* StorageDeviceInfo::GetIoStat() {
  return &iostat_;
}

std::string StorageDeviceInfo::GetDeviceName() const {
  return adapter_->GetDeviceName();
}

std::string StorageDeviceInfo::GetModel() const {
  return adapter_->GetModel();
}

}  // namespace diagnostics
