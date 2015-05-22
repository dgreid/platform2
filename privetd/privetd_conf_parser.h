// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIVETD_PRIVETD_CONF_PARSER_H_
#define PRIVETD_PRIVETD_CONF_PARSER_H_

#include <set>
#include <string>

#include <chromeos/key_value_store.h>

namespace privetd {

extern const char kWiFiBootstrapInterfaces[];

enum class WiFiBootstrapMode {
  kDisabled,
  kManual,
  kAutomatic,
};

enum class GcdBootstrapMode {
  kDisabled,
  kManual,
  kAutomatic,
};

enum class PairingType;

class PrivetdConfigParser final {
 public:
  PrivetdConfigParser();

  bool Parse(const chromeos::KeyValueStore& config_store);

  WiFiBootstrapMode wifi_bootstrap_mode() const { return wifi_bootstrap_mode_; }
  GcdBootstrapMode gcd_bootstrap_mode() const { return gcd_bootstrap_mode_; }
  const std::set<std::string>& automatic_wifi_interfaces() const {
    return automatic_wifi_interfaces_;
  }
  uint32_t connect_timeout_seconds() const { return connect_timeout_seconds_; }
  uint32_t bootstrap_timeout_seconds() const {
    return bootstrap_timeout_seconds_;
  }
  uint32_t monitor_timeout_seconds() const { return monitor_timeout_seconds_; }
  const std::set<PairingType>& pairing_modes() { return pairing_modes_; }
  const base::FilePath& embedded_code_path() const {
    return embedded_code_path_;
  }

 private:
  WiFiBootstrapMode wifi_bootstrap_mode_;
  GcdBootstrapMode gcd_bootstrap_mode_;
  std::set<std::string> automatic_wifi_interfaces_;
  uint32_t connect_timeout_seconds_;
  uint32_t bootstrap_timeout_seconds_;
  uint32_t monitor_timeout_seconds_;
  std::set<PairingType> pairing_modes_;
  base::FilePath embedded_code_path_;
};

}  // namespace privetd

#endif  // PRIVETD_PRIVETD_CONF_PARSER_H_
