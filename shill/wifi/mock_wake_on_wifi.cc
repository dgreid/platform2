// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/mock_wake_on_wifi.h"

#include <gmock/gmock.h>

namespace shill {

MockWakeOnWiFi::MockWakeOnWiFi(NetlinkManager *netlink_manager,
                               EventDispatcher *dispatcher, Metrics *metrics)
    : WakeOnWiFi(netlink_manager, dispatcher, metrics,
                 RecordWakeReasonCallback()) {}

MockWakeOnWiFi::~MockWakeOnWiFi() {}

}  // namespace shill
