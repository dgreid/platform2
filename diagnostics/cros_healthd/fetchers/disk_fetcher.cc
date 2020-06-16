// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"

#include <libudev.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/device_info.h"
#include "diagnostics/cros_healthd/utils/storage/device_lister.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kDevStatFileName[] = "stat";

constexpr char kDevStatRegex[] =
    R"(\s*\d+\s+\d+\s+(\d+)\s+(\d+)\s+\d+\s+\d+\s+(\d+)\s+(\d+))";

constexpr char kSysBlockPath[] = "sys/block/";

// POD struct which holds the number of sectors read and written by a device.
struct SectorStats {
  uint64_t read;
  uint64_t written;
};

// Look through all the block devices and find the ones that are explicitly
// non-removable.
std::vector<base::FilePath> GetNonRemovableBlockDevices(
    const base::FilePath& root) {
  std::vector<base::FilePath> res;
  StorageDeviceLister lister;
  auto device_names = lister.ListDevices(root);

  for (auto d : device_names) {
    res.push_back(root.Append(kSysBlockPath).Append(d));
  }

  return res;
}

// Fill the output with a colon-separated list of subsystems. For example,
// "block:mmc:mmc_host:pci". Similar output is returned by `lsblk -o
// SUBSYSTEMS`. If the call is successful, |subsystem_output| is populated with
// the a string of udev subsystems and base::nullopt is returned. If an error
// occurred, a ProbeError is returned and |subsystem_output| does not contain
// valid information.
base::Optional<mojo_ipc::ProbeErrorPtr> GetUdevDeviceSubsystems(
    udev_device* input_device, std::string* subsystem_output) {
  DCHECK(input_device);
  DCHECK(subsystem_output);
  // |subsystems| will track the stack of subsystems that this device uses.
  std::vector<std::string> subsystems;

  for (udev_device* device = input_device; device != nullptr;
       device = udev_device_get_parent(device)) {
    const char* subsystem = udev_device_get_subsystem(device);
    if (subsystem != nullptr) {
      subsystems.push_back(subsystem);
    }
  }

  if (subsystems.empty()) {
    const char* devnode = udev_device_get_devnode(input_device);
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Unable to collect any subsystems for device " +
            (devnode ? std::string(devnode) : "<unknown>"));
  }

  *subsystem_output = base::JoinString(subsystems, ":");
  return base::nullopt;
}

// When successful, populates |read_time_seconds|, |write_time_seconds| and
// |sector_stats| with information from the disk corresponding to |sys_path| and
// returns base::nullopt. On failure, returns an appropriate error, and none of
// the output variables are valid.
base::Optional<mojo_ipc::ProbeErrorPtr> GetReadWriteStats(
    const base::FilePath& sys_path,
    uint64_t* read_time_seconds,
    uint64_t* write_time_seconds,
    SectorStats* sector_stats) {
  DCHECK(read_time_seconds);
  DCHECK(write_time_seconds);
  DCHECK(sector_stats);

  std::string stat_contents;
  if (!ReadAndTrimString(sys_path, kDevStatFileName, &stat_contents)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read " + sys_path.Append(kDevStatFileName).value());
  }

  std::string read_time_ms;
  std::string write_time_ms;
  std::string sectors_read;
  std::string sectors_written;
  if (!RE2::PartialMatch(stat_contents, kDevStatRegex, &sectors_read,
                         &read_time_ms, &sectors_written, &write_time_ms)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Unable to parse " + sys_path.Append(kDevStatFileName).value() + ": " +
            stat_contents);
  }

  uint64_t read_time_ms_int;
  if (!base::StringToUint64(read_time_ms, &read_time_ms_int)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert read_time_ms to unsigned integer: " + read_time_ms);
  }

  uint64_t write_time_ms_int;
  if (!base::StringToUint64(write_time_ms, &write_time_ms_int)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert write_time_ms to unsigned integer: " +
            write_time_ms);
  }

  if (!base::StringToUint64(sectors_read, &sector_stats->read)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert sectors_read to unsigned integer: " + sectors_read);
  }

  if (!base::StringToUint64(sectors_written, &sector_stats->written)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Failed to convert sectors_written to unsigned integer: " +
            sectors_written);
  }

  // Convert from ms to seconds.
  *read_time_seconds = read_time_ms_int / 1000;
  *write_time_seconds = write_time_ms_int / 1000;

  return base::nullopt;
}

}  // namespace

DiskFetcher::DiskFetcher() = default;

DiskFetcher::~DiskFetcher() = default;

mojo_ipc::NonRemovableBlockDeviceResultPtr
DiskFetcher::FetchNonRemovableBlockDevicesInfo(const base::FilePath& root) {
  std::vector<mojo_ipc::NonRemovableBlockDeviceInfoPtr> devices{};

  for (const auto& sys_path : GetNonRemovableBlockDevices(root)) {
    VLOG(1) << "Processing the node " << sys_path.value();

    // TODO(dlunev): factor devnode and subsystem retrieval into a class,
    // ideally into a factory or StorageDeviceInfo itself.
    base::FilePath devnode_path;
    std::string subsystem;
    auto error = GatherSysPathRelatedInfo(sys_path, &devnode_path, &subsystem);
    if (error.has_value()) {
      return mojo_ipc::NonRemovableBlockDeviceResult::NewError(
          std::move(error.value()));
    }
    // TODO(dlunev): this shall be persisted across probes.
    std::unique_ptr<StorageDeviceInfo> dev_info =
        std::make_unique<StorageDeviceInfo>(sys_path, devnode_path, subsystem);

    mojo_ipc::NonRemovableBlockDeviceInfoPtr info;
    error = FetchNonRemovableBlockDeviceInfo(dev_info, &info);
    if (error.has_value()) {
      return mojo_ipc::NonRemovableBlockDeviceResult::NewError(
          std::move(error.value()));
    }
    DCHECK_NE(info->path, "");
    DCHECK_NE(info->size, 0);
    DCHECK_NE(info->type, "");
    devices.push_back(std::move(info));
  }

  return mojo_ipc::NonRemovableBlockDeviceResult::NewBlockDeviceInfo(
      std::move(devices));
}

base::Optional<mojo_ipc::ProbeErrorPtr>
DiskFetcher::FetchNonRemovableBlockDeviceInfo(
    const std::unique_ptr<StorageDeviceInfo>& dev_info,
    mojo_ipc::NonRemovableBlockDeviceInfoPtr* output_info) {
  DCHECK(output_info);
  mojo_ipc::NonRemovableBlockDeviceInfo info;

  SectorStats sector_stats;
  auto error = GetReadWriteStats(
      dev_info->GetSysPath(), &info.read_time_seconds_since_last_boot,
      &info.write_time_seconds_since_last_boot, &sector_stats);
  if (error.has_value())
    return error;

  info.path = dev_info->GetDevNodePath().value();
  info.type = dev_info->GetSubsystem();

  auto res = dev_info->GetSizeBytes();
  if (!res.ok()) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  res.status().ToString());
  }
  info.size = res.value();

  res = dev_info->GetBlockSizeBytes();
  if (!res.ok()) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  res.status().ToString());
  }
  uint64_t sector_size = res.value();

  // Convert from sectors to bytes.
  info.bytes_written_since_last_boot = sector_size * sector_stats.written;
  info.bytes_read_since_last_boot = sector_size * sector_stats.read;

  const auto device_path = dev_info->GetSysPath().Append("device");

  // Not all devices in sysfs have a model/name, so ignore failure here.
  if (!ReadAndTrimString(device_path, "model", &info.name)) {
    ReadAndTrimString(device_path, "name", &info.name);
  }

  // Not all devices in sysfs have a serial, so ignore the return code.
  ReadInteger(device_path, "serial", &base::HexStringToUInt, &info.serial);

  uint64_t manfid = 0;
  if (ReadInteger(device_path, "manfid", &base::HexStringToUInt64, &manfid)) {
    DCHECK_EQ(manfid & 0xFF, manfid);
    info.manufacturer_id = manfid;
  }

  *output_info = info.Clone();
  return base::nullopt;
}

base::Optional<mojo_ipc::ProbeErrorPtr> DiskFetcher::GatherSysPathRelatedInfo(
    const base::FilePath& sys_path,
    base::FilePath* devnode_path,
    std::string* subsystems) {
  DCHECK(devnode_path);
  DCHECK(subsystems);

  udev* udev = udev_new();
  if (udev == nullptr) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Unable to get udev reference when processing " + sys_path.value());
  }

  udev_device* device =
      udev_device_new_from_syspath(udev, sys_path.value().c_str());
  if (device == nullptr) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Unable to get udev_device for " + sys_path.value());
  }

  auto error = GetUdevDeviceSubsystems(device, subsystems);
  if (error.has_value()) {
    error.value()->msg = "Unable to get the udev device subsystems for " +
                         sys_path.value() + ": " + error.value()->msg;
    return error;
  }

  *devnode_path = base::FilePath{udev_device_get_devnode(device)};
  udev_device_unref(device);
  udev_unref(udev);
  return base::nullopt;
}

}  // namespace diagnostics
