// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_SYSTEM_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_SYSTEM_CONFIG_H_

#include <string>

#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace diagnostics {

class FakeSystemConfig final : public SystemConfigInterface {
 public:
  FakeSystemConfig();
  FakeSystemConfig(const FakeSystemConfig&) = delete;
  FakeSystemConfig& operator=(const FakeSystemConfig&) = delete;
  ~FakeSystemConfig() override;

  // SystemConfigInterface overrides.
  bool FioSupported() override;
  bool HasBacklight() override;
  bool HasBattery() override;
  bool HasSmartBattery() override;
  bool HasSkuNumber() override;
  bool NvmeSupported() override;
  bool SmartCtlSupported() override;
  std::string GetMarketingName() override;

  // Setters for FakeSystemConfig attributes.
  void SetFioSupported(bool value);
  void SetHasBacklight(bool value);
  void SetHasBattery(bool value);
  void SetHasSmartBattery(bool value);
  void SetHasSkuNumber(bool value);
  void SetNvmeSupported(bool value);
  void SetSmartCtrlSupported(bool value);
  void SetMarketingName(const std::string& value);

 private:
  bool fio_supported_ = true;
  bool has_backlight_ = true;
  bool has_battery_ = true;
  bool has_smart_battery_ = true;
  bool has_sku_number_property_ = true;
  bool nvme_supported_ = true;
  bool smart_ctrl_supported_ = true;
  std::string marketing_name_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_SYSTEM_CONFIG_H_
