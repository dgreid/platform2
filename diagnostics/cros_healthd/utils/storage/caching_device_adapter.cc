// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/storage/caching_device_adapter.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <base/optional.h>

#include "diagnostics/cros_healthd/utils/storage/storage_device_adapter.h"

namespace diagnostics {

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

StatusOr<std::string> CachingDeviceAdapter::GetModel() const {
  if (!model_.has_value()) {
    ASSIGN_OR_RETURN(model_, adapter_->GetModel());
  }
  return model_.value();
}

}  // namespace diagnostics
