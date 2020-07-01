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
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/default_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/disk_iostat.h"
#include "diagnostics/cros_healthd/utils/storage/emmc_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/nvme_device_adapter.h"
#include "diagnostics/cros_healthd/utils/storage/status_macros.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"
#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"

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

mojo_ipc::ErrorType StatusCodeToMojoError(StatusCode code) {
  switch (code) {
    case StatusCode::kUnavailable:
      return mojo_ipc::ErrorType::kFileReadError;
    case StatusCode::kInvalidArgument:
      return mojo_ipc::ErrorType::kParseError;
    case StatusCode::kInternal:
      return mojo_ipc::ErrorType::kSystemUtilityError;
    default:
      NOTREACHED() << "Unexpected error code: " << static_cast<int>(code);
      return mojo_ipc::ErrorType::kSystemUtilityError;
  }
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

base::Optional<chromeos::cros_healthd::mojom::ProbeErrorPtr>
StorageDeviceInfo::PopulateDeviceInfo(
    mojo_ipc::NonRemovableBlockDeviceInfo* output_info) {
  auto status = PopulateDeviceInfoImpl(output_info);
  if (status.ok())
    return base::nullopt;
  return CreateAndLogProbeError(StatusCodeToMojoError(status.code()),
                                status.message());
}

Status StorageDeviceInfo::PopulateDeviceInfoImpl(
    mojo_ipc::NonRemovableBlockDeviceInfo* output_info) {
  DCHECK(output_info);

  output_info->path = dev_node_path_.value();
  output_info->type = subsystem_;

  RETURN_IF_ERROR(iostat_.Update());
  ASSIGN_OR_RETURN(output_info->size,
                   platform_->GetDeviceSizeBytes(dev_node_path_));
  ASSIGN_OR_RETURN(uint64_t sector_size,
                   platform_->GetDeviceBlockSizeBytes(dev_node_path_));

  output_info->read_time_seconds_since_last_boot =
      static_cast<uint64_t>(iostat_.GetReadTime().InSeconds());
  output_info->write_time_seconds_since_last_boot =
      static_cast<uint64_t>(iostat_.GetWriteTime().InSeconds());
  output_info->io_time_seconds_since_last_boot =
      static_cast<uint64_t>(iostat_.GetIoTime().InSeconds());

  auto discard_time = iostat_.GetDiscardTime();
  if (discard_time.has_value()) {
    output_info->discard_time_seconds_since_last_boot =
        mojo_ipc::UInt64Value::New(
            static_cast<uint64_t>(discard_time.value().InSeconds()));
  }

  // Convert from sectors to bytes.
  output_info->bytes_written_since_last_boot =
      sector_size * iostat_.GetWrittenSectors();
  output_info->bytes_read_since_last_boot =
      sector_size * iostat_.GetReadSectors();

  output_info->name = adapter_->GetModel();

  return Status::OkStatus();
}

void StorageDeviceInfo::PopulateLegacyFields(
    mojo_ipc::NonRemovableBlockDeviceInfo* output_info) {
  DCHECK(output_info);

  constexpr char kLegacySerialFile[] = "device/serial";
  constexpr char kLegacyManfidFile[] = "device/manfid";

  // Not all devices in sysfs have a serial, so ignore the return code.
  ReadInteger(dev_sys_path_, kLegacySerialFile, &base::HexStringToUInt,
              &output_info->serial);

  uint64_t manfid = 0;
  if (ReadInteger(dev_sys_path_, kLegacyManfidFile, &base::HexStringToUInt64,
                  &manfid)) {
    DCHECK_EQ(manfid & 0xFF, manfid);
    output_info->manufacturer_id = manfid;
  }
}

}  // namespace diagnostics
