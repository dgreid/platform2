// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_LINK_MONITOR_H_
#define SHILL_MOCK_LINK_MONITOR_H_

#include "shill/link_monitor.h"

#include <gmock/gmock.h>

namespace shill {

class MockLinkMonitor : public LinkMonitor {
 public:
  MockLinkMonitor();
  MockLinkMonitor(const MockLinkMonitor&) = delete;
  MockLinkMonitor& operator=(const MockLinkMonitor&) = delete;

  ~MockLinkMonitor() override;

  MOCK_METHOD(bool, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void, OnAfterResume, (), (override));
  MOCK_METHOD(int, GetResponseTimeMilliseconds, (), (const, override));
  MOCK_METHOD(bool, IsGatewayFound, (), (const, override));
};

}  // namespace shill

#endif  // SHILL_MOCK_LINK_MONITOR_H_
