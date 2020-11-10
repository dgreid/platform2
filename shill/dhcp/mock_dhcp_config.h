// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_MOCK_DHCP_CONFIG_H_
#define SHILL_DHCP_MOCK_DHCP_CONFIG_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/dhcp/dhcp_config.h"

namespace shill {

class MockDHCPConfig : public DHCPConfig {
 public:
  MockDHCPConfig(ControlInterface* control_interface,
                 const std::string& device_name);
  MockDHCPConfig(const MockDHCPConfig&) = delete;
  MockDHCPConfig& operator=(const MockDHCPConfig&) = delete;

  ~MockDHCPConfig() override;

  void ProcessEventSignal(const std::string& reason,
                          const KeyValueStore& configuration) override;
  void ProcessStatusChangeSignal(const std::string& status) override;

  MOCK_METHOD(bool, RequestIP, (), (override));
  MOCK_METHOD(bool, ReleaseIP, (ReleaseReason), (override));
  MOCK_METHOD(bool, RenewIP, (), (override));
  MOCK_METHOD(void, set_minimum_mtu, (int), (override));
};

}  // namespace shill

#endif  // SHILL_DHCP_MOCK_DHCP_CONFIG_H_
