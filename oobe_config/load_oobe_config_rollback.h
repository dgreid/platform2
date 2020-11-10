// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_LOAD_OOBE_CONFIG_ROLLBACK_H_
#define OOBE_CONFIG_LOAD_OOBE_CONFIG_ROLLBACK_H_

#include "oobe_config/load_oobe_config_interface.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <power_manager-client/power_manager/dbus-proxies.h>

#include "oobe_config/metrics.h"

namespace oobe_config {

class OobeConfig;
class RollbackData;

// An object of this class has the responsibility of loading the oobe config
// file after rollback.
class LoadOobeConfigRollback : public LoadOobeConfigInterface {
 public:
  LoadOobeConfigRollback(OobeConfig* oobe_config,
                         bool allow_unencrypted,
                         bool skip_reboot_for_testing,
                         org::chromium::PowerManagerProxy* power_manager_proxy);
  LoadOobeConfigRollback(const LoadOobeConfigRollback&) = delete;
  LoadOobeConfigRollback& operator=(const LoadOobeConfigRollback&) = delete;

  ~LoadOobeConfigRollback() = default;

  bool GetOobeConfigJson(std::string* config,
                         std::string* enrollment_domain) override;

 private:
  // Assembles a JSON config for Chrome based on rollback_data. Returns true if
  // |config| is successfully populated during stage 3 of rollback. Returns
  // false to indicate that either rollback was not attempted or there was a
  // failure. During stage 1 of rollback, the process exits before returning.
  bool AssembleConfig(const RollbackData& rollback_data, std::string* config);

  OobeConfig* oobe_config_;
  bool allow_unencrypted_ = false;
  bool skip_reboot_for_testing_ = false;
  org::chromium::PowerManagerProxy* power_manager_proxy_;
  Metrics metrics_;  // For UMA metrics logging.
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_LOAD_OOBE_CONFIG_ROLLBACK_H_
