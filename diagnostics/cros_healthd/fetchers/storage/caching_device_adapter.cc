// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/storage/caching_device_adapter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>

#include "diagnostics/cros_healthd/fetchers/storage/storage_device_adapter.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}

CachingDeviceAdapter::CachingDeviceAdapter(
    std::unique_ptr<StorageDeviceAdapter> adapter)
    : adapter_(std::move(adapter)) {
  DCHECK(adapter_);
}

std::string CachingDeviceAdapter::GetDeviceName() const {
  if (!device_name_.has_value())
    device_name_ = adapter_->GetDeviceName();
  return device_name_.value();
}

StatusOr<mojo_ipc::BlockDeviceVendor> CachingDeviceAdapter::GetVendorId()
    const {
  if (!vendor_id_.has_value()) {
    ASSIGN_OR_RETURN(vendor_id_, adapter_->GetVendorId());
  }
  return vendor_id_.value();
}

StatusOr<mojo_ipc::BlockDeviceProduct> CachingDeviceAdapter::GetProductId()
    const {
  if (!product_id_.has_value()) {
    ASSIGN_OR_RETURN(product_id_, adapter_->GetProductId());
  }
  return product_id_.value();
}

StatusOr<mojo_ipc::BlockDeviceRevision> CachingDeviceAdapter::GetRevision()
    const {
  if (!revision_.has_value()) {
    ASSIGN_OR_RETURN(revision_, adapter_->GetRevision());
  }
  return revision_.value();
}

StatusOr<std::string> CachingDeviceAdapter::GetModel() const {
  if (!model_.has_value()) {
    ASSIGN_OR_RETURN(model_, adapter_->GetModel());
  }
  return model_.value();
}

StatusOr<mojo_ipc::BlockDeviceFirmware>
CachingDeviceAdapter::GetFirmwareVersion() const {
  if (!firmware_.has_value()) {
    ASSIGN_OR_RETURN(firmware_, adapter_->GetFirmwareVersion());
  }
  return firmware_.value();
}

}  // namespace diagnostics
