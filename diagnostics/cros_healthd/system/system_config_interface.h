// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_INTERFACE_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_INTERFACE_H_

namespace diagnostics {

class SystemConfigInterface {
 public:
  virtual ~SystemConfigInterface() = default;

  // Returns if the device has the fio utility.
  virtual bool FioSupported() = 0;

  // Returns if the device has a backlight.
  virtual bool HasBacklight() = 0;

  // Returns if the device has a battery (e.g. not a Chromebox).
  virtual bool HasBattery() = 0;

  // Returns if the device has a SKU number in the VPD fields.
  virtual bool HasSkuNumberProperty() = 0;

  // Returns if the device has a battery with SMART features.
  virtual bool HasSmartBattery() = 0;

  // Returns if the device has an Nvme drive and the associated utilities.
  virtual bool NvmeSupported() = 0;

  // Returns if the device has support for smartctl.
  virtual bool SmartCtlSupported() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_INTERFACE_H_
