// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_MAC80211_MONITOR_H_
#define SHILL_WIFI_MOCK_MAC80211_MONITOR_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/wifi/mac80211_monitor.h"

namespace shill {

class MockMac80211Monitor : public Mac80211Monitor {
 public:
  MockMac80211Monitor(EventDispatcher* dispatcher,
                      const std::string& link_name,
                      size_t queue_length_limit,
                      const base::Closure& on_repair_callback,
                      Metrics* metrics);
  MockMac80211Monitor(const MockMac80211Monitor&) = delete;
  MockMac80211Monitor& operator=(const MockMac80211Monitor&) = delete;

  ~MockMac80211Monitor() override;

  MOCK_METHOD(void, Start, (const std::string&), (override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(void, UpdateConnectedState, (bool), (override));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_MAC80211_MONITOR_H_
