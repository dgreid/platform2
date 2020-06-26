// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_WAKE_ON_WIFI_INTERFACE_H_
#define SHILL_WIFI_WAKE_ON_WIFI_INTERFACE_H_

#include <string>
#include <vector>

#include "shill/callbacks.h"
#include "shill/wifi/wifi.h"

namespace shill {

class ByteString;
class Error;
class Nl80211Message;
class PropertyStore;

// Base class for WakeOnWiFi implementations. This is so stub and mock
// implementations don't pull in e.g. WakeOnWiFi members.
//
// This is just the interface; for explanations of each method and a
// detailed diagram of the state machine, look at wake_on_wifi.h.

class WakeOnWiFiInterface {
 public:
  using InitiateScanCallback = base::Callback<void(const WiFi::FreqSet&)>;
  // Callback used to report the wake reason for the current dark resume to
  // powerd.
  using RecordWakeReasonCallback = base::Callback<void(const std::string&)>;

  // Types of triggers that we can program the NIC to wake the WiFi device.
  enum WakeOnWiFiTrigger {
    kWakeTriggerUnsupported = 0,  // Used for reporting, not programming NIC.
    kWakeTriggerPattern = 1,
    kWakeTriggerDisconnect = 2,
    kWakeTriggerSSID = 3
  };

  virtual ~WakeOnWiFiInterface() = default;

  virtual void InitPropertyStore(PropertyStore* store) = 0;
  virtual void StartMetricsTimer() = 0;
  virtual void AddWakeOnPacketConnection(const std::string& ip_endpoint,
                                         Error* error) = 0;
  virtual void AddWakeOnPacketOfTypes(
      const std::vector<std::string>& packet_types, Error* error) = 0;
  virtual void RemoveWakeOnPacketConnection(const std::string& ip_endpoint,
                                            Error* error) = 0;
  virtual void RemoveWakeOnPacketOfTypes(
      const std::vector<std::string>& packet_types, Error* error) = 0;
  virtual void RemoveAllWakeOnPacketConnections(Error* error) = 0;
  virtual void ParseWakeOnWiFiCapabilities(
      const Nl80211Message& nl80211_message) = 0;
  virtual void OnBeforeSuspend(
      bool is_connected,
      const std::vector<ByteString>& allowed_ssids,
      const ResultCallback& done_callback,
      const base::Closure& renew_dhcp_lease_callback,
      const base::Closure& remove_supplicant_networks_callback,
      bool have_dhcp_lease,
      uint32_t time_to_next_lease_renewal) = 0;
  virtual void OnAfterResume() = 0;
  virtual void OnDarkResume(
      bool is_connected,
      const std::vector<ByteString>& allowed_ssids,
      const ResultCallback& done_callback,
      const base::Closure& renew_dhcp_lease_callback,
      const InitiateScanCallback& initiate_scan_callback,
      const base::Closure& remove_supplicant_networks_callback) = 0;
  virtual void OnConnectedAndReachable(bool start_lease_renewal_timer,
                                       uint32_t time_to_next_lease_renewal) = 0;
  virtual void ReportConnectedToServiceAfterWake(bool is_connected,
                                                 int seconds_in_suspend) = 0;
  virtual void OnNoAutoConnectableServicesAfterScan(
      const std::vector<ByteString>& allowed_ssids,
      const base::Closure& remove_supplicant_networks_callback,
      const InitiateScanCallback& initiate_scan_callback) = 0;
  virtual void OnScanStarted(bool is_active_scan) = 0;
  virtual bool InDarkResume() = 0;
  virtual void OnWiphyIndexReceived(uint32_t index) = 0;
};

}  // namespace shill

#endif  // SHILL_WIFI_WAKE_ON_WIFI_INTERFACE_H_
