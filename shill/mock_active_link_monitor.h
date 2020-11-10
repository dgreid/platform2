// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_ACTIVE_LINK_MONITOR_H_
#define SHILL_MOCK_ACTIVE_LINK_MONITOR_H_

#include "shill/active_link_monitor.h"

#include <base/macros.h>
#include <gmock/gmock.h>

namespace shill {

class MockActiveLinkMonitor : public ActiveLinkMonitor {
 public:
  MockActiveLinkMonitor();
  MockActiveLinkMonitor(const MockActiveLinkMonitor&) = delete;
  MockActiveLinkMonitor& operator=(const MockActiveLinkMonitor&) = delete;

  ~MockActiveLinkMonitor() override;

  MOCK_METHOD(bool, Start, (int), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(const ByteString&, gateway_mac_address, (), (const, override));
  MOCK_METHOD(void, set_gateway_mac_address, (const ByteString&), (override));
  MOCK_METHOD(bool, gateway_supports_unicast_arp, (), (const, override));
  MOCK_METHOD(void, set_gateway_supports_unicast_arp, (bool), (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_ACTIVE_LINK_MONITOR_H_
