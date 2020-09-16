// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_

#include <string>

#include <chromeos/chromeos-config/libcros_config/cros_config_interface.h>
#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/system/system_config_interface.h"

namespace diagnostics {

class SystemConfig final : public SystemConfigInterface {
 public:
  explicit SystemConfig(brillo::CrosConfigInterface* cros_config);
  // Constructor that overrides root_dir is only meant to be used for testing.
  explicit SystemConfig(brillo::CrosConfigInterface* cros_config,
                        const base::FilePath& root_dir);
  SystemConfig(const SystemConfig&) = delete;
  SystemConfig& operator=(const SystemConfig&) = delete;
  ~SystemConfig() override;

  // SystemConfigInterface overrides:
  bool FioSupported() override;
  bool HasBacklight() override;
  bool HasBattery() override;
  bool HasSmartBattery() override;
  bool HasSkuNumber() override;
  bool NvmeSupported() override;
  bool SmartCtlSupported() override;
  bool IsWilcoDevice() override;
  std::string GetMarketingName() override;

 private:
  // Unowned pointer. The CrosConfigInterface should outlive this instance.
  brillo::CrosConfigInterface* cros_config_;
  base::FilePath root_dir_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_SYSTEM_CONFIG_H_
