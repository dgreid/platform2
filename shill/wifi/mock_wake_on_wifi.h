// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_
#define SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/net/nl80211_message.h"
#include "shill/wifi/wake_on_wifi.h"

namespace shill {

class MockWakeOnWiFi : public WakeOnWiFi {
 public:
  MockWakeOnWiFi(NetlinkManager *netlink_manager, EventDispatcher *dispatcher,
                 Metrics *metrics);
  ~MockWakeOnWiFi() override;

  MOCK_METHOD0(OnAfterResume, void());
  MOCK_METHOD7(OnBeforeSuspend,
               void(bool is_connected,
                    const std::vector<ByteString> &ssid_whitelist,
                    const ResultCallback &done_callback,
                    const base::Closure &renew_dhcp_lease_callback,
                    const base::Closure &remove_supplicant_networks_callback,
                    bool have_dhcp_lease, uint32_t time_to_next_lease_renewal));
  MOCK_METHOD6(OnDarkResume,
               void(bool is_connected,
                    const std::vector<ByteString> &ssid_whitelist,
                    const ResultCallback &done_callback,
                    const base::Closure &renew_dhcp_lease_callback,
                    const base::Closure &initiate_scan_callback,
                    const base::Closure &remove_supplicant_networks_callback));
  MOCK_METHOD2(OnDHCPLeaseObtained, void(bool start_lease_renewal_timer,
                                         uint32_t time_to_next_lease_renewal));
  MOCK_METHOD1(ReportConnectedToServiceAfterWake, void(bool is_connected));
  MOCK_METHOD2(OnNoAutoConnectableServicesAfterScan,
               void(const std::vector<ByteString> &ssid_whitelist,
                    const base::Closure &remove_supplicant_networks_callback));
  MOCK_METHOD1(OnWakeupReasonReceived,
               void(const NetlinkMessage &netlink_message));
  MOCK_METHOD0(NotifyWakeupReasonReceived, void());
  MOCK_METHOD1(NotifyWakeOnWiFiOnDarkResume,
               void(WakeOnWiFi::WakeOnWiFiTrigger reason));
  MOCK_METHOD1(OnWiphyIndexReceived, void(uint32_t));
  MOCK_METHOD1(ParseWakeOnWiFiCapabilities,
               void(const Nl80211Message &nl80211_message));
  MOCK_METHOD1(OnScanStarted, void(bool is_active_scan));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockWakeOnWiFi);
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_
