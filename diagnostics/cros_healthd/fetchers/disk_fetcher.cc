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
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/device_info.h"
#include "diagnostics/cros_healthd/utils/storage/device_lister.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

constexpr char kSysBlockPath[] = "sys/block/";

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
        StorageDeviceInfo::Create(sys_path, devnode_path, subsystem);
    if (!dev_info) {
      return mojo_ipc::NonRemovableBlockDeviceResult::NewError(
          CreateAndLogProbeError(
              mojo_ipc::ErrorType::kSystemUtilityError,
              "Unable to create device info object for " + sys_path.value()));
    }

    mojo_ipc::NonRemovableBlockDeviceInfo info;
    error = dev_info->PopulateDeviceInfo(&info);
    if (error.has_value()) {
      return mojo_ipc::NonRemovableBlockDeviceResult::NewError(
          std::move(error.value()));
    }
    // Doesn't matter if the legacy field spew an error.
    dev_info->PopulateLegacyFields(&info);
    devices.push_back(info.Clone());
  }

  return mojo_ipc::NonRemovableBlockDeviceResult::NewBlockDeviceInfo(
      std::move(devices));
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
