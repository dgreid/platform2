// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_METRICS_H_
#define SHILL_MOCK_METRICS_H_

#include <string>

#include "shill/metrics.h"

#include <gmock/gmock.h>

#include "shill/wifi/wake_on_wifi.h"

namespace shill {

class MockMetrics : public Metrics {
 public:
  explicit MockMetrics(EventDispatcher *dispatcher);
  ~MockMetrics() override;

  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD4(AddServiceStateTransitionTimer,
               void(const Service &service, const std::string &histogram_name,
                    Service::ConnectState start_state,
                    Service::ConnectState stop_state));
  MOCK_METHOD1(DeregisterDevice, void(int interface_index));
  MOCK_METHOD1(NotifyDeviceScanStarted, void(int interface_index));
  MOCK_METHOD1(NotifyDeviceScanFinished, void(int interface_index));
  MOCK_METHOD1(ResetScanTimer, void(int interface_index));
  MOCK_METHOD2(NotifyDeviceConnectStarted, void(int interface_index,
                                                bool is_auto_connecting));
  MOCK_METHOD1(NotifyDeviceConnectFinished, void(int interface_index));
  MOCK_METHOD1(ResetConnectTimer, void(int interface_index));
  MOCK_METHOD1(NotifyDefaultServiceChanged, void(const Service *service));
  MOCK_METHOD2(NotifyServiceStateChanged,
               void(const Service &service, Service::ConnectState new_state));
  MOCK_METHOD2(Notify80211Disconnect, void(WiFiDisconnectByWhom by_whom,
                                           IEEE_80211::WiFiReasonCode reason));
  MOCK_METHOD0(Notify3GPPRegistrationDelayedDropPosted, void());
  MOCK_METHOD0(Notify3GPPRegistrationDelayedDropCanceled, void());
  MOCK_METHOD0(NotifyCorruptedProfile, void());
  MOCK_METHOD3(SendEnumToUMA, bool(const std::string &name, int sample,
                                   int max));
  MOCK_METHOD5(SendToUMA, bool(const std::string &name, int sample, int min,
                               int max, int num_buckets));
  MOCK_METHOD1(NotifyWifiAutoConnectableServices, void(int num_service));
  MOCK_METHOD1(NotifyWifiAvailableBSSes, void(int num_bss));
  MOCK_METHOD1(NotifyServicesOnSameNetwork, void(int num_service));
  MOCK_METHOD1(NotifyUserInitiatedEvent, void(int event));
  MOCK_METHOD1(NotifyWifiTxBitrate, void(int bitrate));
  MOCK_METHOD2(NotifyUserInitiatedConnectionResult,
               void(const std::string &name, int result));
  MOCK_METHOD2(NotifyUserInitiatedConnectionFailureReason,
               void(const std::string &name,
                    const Service::ConnectFailure failure));
  MOCK_METHOD2(NotifyNetworkProblemDetected,
               void(Technology::Identifier technology_id, int reason));
  MOCK_METHOD2(NotifyFallbackDNSTestResult,
               void(Technology::Identifier technology_id, int result));
  MOCK_METHOD1(NotifyDeviceConnectionStatus,
               void(Metrics::ConnectionStatus status));
  MOCK_METHOD1(NotifyDhcpClientStatus, void(Metrics::DhcpClientStatus status));
  MOCK_METHOD2(NotifyNetworkConnectionIPType,
               void(Technology::Identifier technology_id,
                    Metrics::NetworkConnectionIPType type));
  MOCK_METHOD2(NotifyIPv6ConnectivityStatus,
               void(Technology::Identifier technology_id, bool status));
  MOCK_METHOD2(NotifyDevicePresenceStatus,
               void(Technology::Identifier technology_id, bool status));
  MOCK_METHOD2(NotifyUnreliableLinkSignalStrength,
               void(Technology::Identifier technology_id, int signal_strength));
  MOCK_METHOD1(NotifyVerifyWakeOnWiFiSettingsResult,
               void(VerifyWakeOnWiFiSettingsResult result));
  MOCK_METHOD1(NotifyConnectedToServiceAfterWake,
               void(WiFiConnectionStatusAfterWake status));
  MOCK_METHOD0(NotifyWakeOnWiFiThrottled, void());
  MOCK_METHOD0(NotifySuspendWithWakeOnWiFiEnabledDone, void());
  MOCK_METHOD0(NotifyDarkResumeInitiateScan, void());
  MOCK_METHOD0(NotifyWakeupReasonReceived, void());
  MOCK_METHOD1(NotifyWakeOnWiFiOnDarkResume,
               void(WakeOnWiFi::WakeOnWiFiTrigger reason));
  MOCK_METHOD1(NotifyScanStartedInDarkResume, void(bool is_active_scan));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockMetrics);
};

}  // namespace shill

#endif  // SHILL_MOCK_METRICS_H_
