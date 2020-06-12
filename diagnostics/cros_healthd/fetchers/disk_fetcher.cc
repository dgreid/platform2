// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"

#include <utility>
#include <vector>

#include <base/optional.h>
#include <brillo/udev/udev.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/storage/device_lister.h"
#include "diagnostics/cros_healthd/utils/storage/device_manager.h"
#include "diagnostics/cros_healthd/utils/storage/device_resolver.h"
#include "diagnostics/cros_healthd/utils/storage/status_macros.h"
#include "diagnostics/cros_healthd/utils/storage/statusor.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

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

mojo_ipc::NonRemovableBlockDeviceResultPtr StatusToProbeError(
    const Status& status) {
  return mojo_ipc::NonRemovableBlockDeviceResult::NewError(
      CreateAndLogProbeError(StatusCodeToMojoError(status.code()),
                             status.message()));
}

}  // namespace

Status DiskFetcher::InitManager(const base::FilePath& root) {
  auto udev = brillo::Udev::Create();
  if (!udev)
    return Status(StatusCode::kInternal, "Unable to create udev interface");

  ASSIGN_OR_RETURN(auto resolver, StorageDeviceResolver::Create(root));
  manager_.reset(new StorageDeviceManager(
      std::make_unique<StorageDeviceLister>(), std::move(resolver),
      std::move(udev), std::make_unique<Platform>()));

  return Status::OkStatus();
}

mojo_ipc::NonRemovableBlockDeviceResultPtr
DiskFetcher::FetchNonRemovableBlockDevicesInfo(const base::FilePath& root) {
  if (!manager_) {
    auto status = InitManager(root);
    if (!status.ok())
      return StatusToProbeError(status);
  }

  StatusOr<std::vector<
      chromeos::cros_healthd::mojom::NonRemovableBlockDeviceInfoPtr>>
      devices_or = manager_->FetchDevicesInfo(root);

  if (devices_or.ok()) {
    return mojo_ipc::NonRemovableBlockDeviceResult::NewBlockDeviceInfo(
        std::move(devices_or.value()));
  }
  return StatusToProbeError(devices_or.status());
}

}  // namespace diagnostics
