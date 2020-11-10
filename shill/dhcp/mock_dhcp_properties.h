// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DHCP_MOCK_DHCP_PROPERTIES_H_
#define SHILL_DHCP_MOCK_DHCP_PROPERTIES_H_

#include "shill/dhcp/dhcp_properties.h"

#include <string>

#include <gmock/gmock.h>

namespace shill {

class MockDhcpProperties : public DhcpProperties {
 public:
  MockDhcpProperties();
  MockDhcpProperties(const MockDhcpProperties&) = delete;
  MockDhcpProperties& operator=(const MockDhcpProperties&) = delete;

  ~MockDhcpProperties() override;

  MOCK_METHOD(void,
              Save,
              (StoreInterface*, const std::string&),
              (const, override));
  MOCK_METHOD(void,
              Load,
              (const StoreInterface*, const std::string&),
              (override));
};

}  // namespace shill

#endif  // SHILL_DHCP_MOCK_DHCP_PROPERTIES_H_
