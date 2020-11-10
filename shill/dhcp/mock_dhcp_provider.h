// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_MOCK_DHCP_PROVIDER_H_
#define SHILL_DHCP_MOCK_DHCP_PROVIDER_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/dhcp/dhcp_config.h"
#include "shill/dhcp/dhcp_properties.h"
#include "shill/dhcp/dhcp_provider.h"
#include "shill/refptr_types.h"

namespace shill {

class MockDHCPProvider : public DHCPProvider {
 public:
  MockDHCPProvider();
  MockDHCPProvider(const MockDHCPProvider&) = delete;
  MockDHCPProvider& operator=(const MockDHCPProvider&) = delete;

  ~MockDHCPProvider() override;

  MOCK_METHOD(void,
              Init,
              (ControlInterface*, EventDispatcher*, Metrics*),
              (override));
  MOCK_METHOD(
      DHCPConfigRefPtr,
      CreateIPv4Config,
      (const std::string&, const std::string&, bool, const DhcpProperties&),
      (override));
#ifndef DISABLE_DHCPV6
  MOCK_METHOD(DHCPConfigRefPtr,
              CreateIPv6Config,
              (const std::string&, const std::string&),
              (override));
#endif
  MOCK_METHOD(void, BindPID, (int, const DHCPConfigRefPtr&), (override));
  MOCK_METHOD(void, UnbindPID, (int), (override));
};

}  // namespace shill

#endif  // SHILL_DHCP_MOCK_DHCP_PROVIDER_H_
