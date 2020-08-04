// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_DEVICE_FACTORY_IMPL_H_
#define BIOD_CROS_FP_DEVICE_FACTORY_IMPL_H_

#include <memory>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_device_factory.h"
#include "biod/cros_fp_device_interface.h"

namespace biod {

class CrosFpDeviceFactoryImpl : public CrosFpDeviceFactory {
 public:
  ~CrosFpDeviceFactoryImpl() override = default;
  std::unique_ptr<CrosFpDeviceInterface> Create(
      const MkbpCallback& callback,
      BiodMetricsInterface* biod_metrics) override {
    auto dev = std::make_unique<CrosFpDevice>(
        biod_metrics, std::make_unique<EcCommandFactory>());
    dev->SetMkbpEventCallback(callback);
    if (!dev->Init()) {
      return nullptr;
    }
    return dev;
  }
};

}  // namespace biod

#endif  // BIOD_CROS_FP_DEVICE_FACTORY_IMPL_H_
