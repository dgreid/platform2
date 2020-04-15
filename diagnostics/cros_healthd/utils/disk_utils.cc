// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/disk_utils.h"

#include <fcntl.h>
#include <libudev.h>
#include <memory>
#include <string>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "diagnostics/common/file_utils.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Look through all the block devices and find the ones that are explicitly
// non-removable.
std::vector<base::FilePath> GetNonRemovableBlockDevices(
    const base::FilePath& root) {
  std::vector<base::FilePath> res;
  const base::FilePath storage_dir_path(root.Append("sys/class/block/"));
  base::FileEnumerator storage_dir_it(storage_dir_path, true,
                                      base::FileEnumerator::SHOW_SYM_LINKS |
                                          base::FileEnumerator::FILES |
                                          base::FileEnumerator::DIRECTORIES);

  while (true) {
    const auto storage_path = storage_dir_it.Next();
    if (storage_path.empty())
      break;

    // Skip Loopback, dm-verity, or zram devices.
    if (base::StartsWith(storage_path.BaseName().value(), "loop",
                         base::CompareCase::SENSITIVE) ||
        base::StartsWith(storage_path.BaseName().value(), "dm-",
                         base::CompareCase::SENSITIVE) ||
        base::StartsWith(storage_path.BaseName().value(), "zram",
                         base::CompareCase::SENSITIVE)) {
      continue;
    }

    // Only return non-removable devices
    int64_t removable = 0;
    if (!ReadInteger(storage_path, "removable", &base::StringToInt64,
                     &removable) ||
        removable) {
      VLOG(1) << "Storage device " << storage_path.value()
              << " does not specify the removable property or is removable.";
      continue;
    }

    res.push_back(storage_path);
  }

  return res;
}

// Gets the size of the drive in bytes, given the /dev node. If the call is
// successful, |size_in_bytes| contains the size of the device and base::nullopt
// is returned. If an error occurred, a ProbeError is returned and
// |size_in_bytes| does not contain valid information.
base::Optional<mojo_ipc::ProbeErrorPtr> GetDriveDeviceSizeInBytes(
    const base::FilePath& dev_path, uint64_t* size_in_bytes) {
  DCHECK(size_in_bytes);
  int fd = open(dev_path.value().c_str(), O_RDONLY, 0);
  if (fd < 0) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kSystemUtilityError,
        "Could not open " + dev_path.value() + " for ioctl access");
  }

  base::ScopedFD scoped_fd(fd);
  uint64_t size = 0;
  int res = ioctl(fd, BLKGETSIZE64, &size);
  if (res != 0) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  "Unable to run ioctl(" + std::to_string(fd) +
                                      ", BLKGETSIZE64, &size) => " +
                                      std::to_string(res) + " for " +
                                      dev_path.value());
  }

  DCHECK_GE(size, 0);
  VLOG(1) << "Found size of " << dev_path.value() << " is "
          << std::to_string(size);

  *size_in_bytes = size;

  return base::nullopt;
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

// Gets the /dev/... name for |sys_path| output parameter, which should be a
// /sys/class/block/... name. This utilizes libudev. Also returns the driver
// |subsystems| output parameter for use in determining the "type" of the block
// device. If the call is successful, base::nullopt is returned. If an error
// occurred, a ProbeError is returned and the output parameters do not contain
// valid information.
base::Optional<mojo_ipc::ProbeErrorPtr> GatherSysPathRelatedInfo(
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

base::Optional<mojo_ipc::ProbeErrorPtr> FetchNonRemovableBlockDeviceInfo(
    const base::FilePath& sys_path,
    mojo_ipc::NonRemovableBlockDeviceInfoPtr* output_info) {
  DCHECK(output_info);
  mojo_ipc::NonRemovableBlockDeviceInfo info;

  base::FilePath devnode_path;
  auto error = GatherSysPathRelatedInfo(sys_path, &devnode_path, &info.type);
  if (error.has_value())
    return error;

  info.path = devnode_path.value();

  error = GetDriveDeviceSizeInBytes(devnode_path, &info.size);
  if (error.has_value())
    return error;

  const auto device_path = sys_path.Append("device");

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

}  // namespace

mojo_ipc::NonRemovableBlockDeviceResultPtr FetchNonRemovableBlockDevicesInfo(
    const base::FilePath& root) {
  // We'll fill out this |devices| vector with the return value.
  std::vector<mojo_ipc::NonRemovableBlockDeviceInfoPtr> devices{};

  for (const base::FilePath& sys_path : GetNonRemovableBlockDevices(root)) {
    VLOG(1) << "Processing the node " << sys_path.value();
    mojo_ipc::NonRemovableBlockDeviceInfoPtr info;
    auto error = FetchNonRemovableBlockDeviceInfo(sys_path, &info);
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

}  // namespace diagnostics
