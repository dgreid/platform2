// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_TRAFFIC_MONITOR_H_
#define SHILL_MOCK_TRAFFIC_MONITOR_H_

#include "shill/traffic_monitor.h"

#include <gmock/gmock.h>

namespace shill {

class MockTrafficMonitor : public TrafficMonitor {
 public:
  MockTrafficMonitor();
  MockTrafficMonitor(const MockTrafficMonitor&) = delete;
  MockTrafficMonitor& operator=(const MockTrafficMonitor&) = delete;

  ~MockTrafficMonitor() override;

  MOCK_METHOD(void, Start, (), (override));
  MOCK_METHOD(void, Stop, (), (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_TRAFFIC_MONITOR_H_
