// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_FACTORY_H_
#define BIOD_CROS_FP_DEVICE_FACTORY_H_

#include <memory>

#include "biod/cros_fp_device_interface.h"

namespace biod {

using MkbpCallback = base::Callback<void(const uint32_t event)>;

class CrosFpDeviceFactory {
 public:
  virtual ~CrosFpDeviceFactory() = default;
  virtual std::unique_ptr<CrosFpDeviceInterface> Create(
      const MkbpCallback& callback, BiodMetricsInterface* biod_metrics) = 0;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_FACTORY_H_
