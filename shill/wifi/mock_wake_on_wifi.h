// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_
#define SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/error.h"
#include "shill/net/nl80211_message.h"
#include "shill/property_store.h"
#include "shill/wifi/wake_on_wifi_interface.h"

namespace shill {

class MockWakeOnWiFi : public WakeOnWiFiInterface {
 public:
  MockWakeOnWiFi();
  MockWakeOnWiFi(const MockWakeOnWiFi&) = delete;
  MockWakeOnWiFi& operator=(const MockWakeOnWiFi&) = delete;

  ~MockWakeOnWiFi() override;

  MOCK_METHOD(void, InitPropertyStore, (PropertyStore * store), (override));
  MOCK_METHOD(void, StartMetricsTimer, (), (override));
  MOCK_METHOD(void,
              AddWakeOnPacketConnection,
              (const std::string&, Error*),
              (override));
  MOCK_METHOD(void,
              AddWakeOnPacketOfTypes,
              (const std::vector<std::string>&, Error*),
              (override));
  MOCK_METHOD(void,
              RemoveWakeOnPacketConnection,
              (const std::string&, Error*),
              (override));
  MOCK_METHOD(void,
              RemoveWakeOnPacketOfTypes,
              (const std::vector<std::string>&, Error*),
              (override));
  MOCK_METHOD(void, RemoveAllWakeOnPacketConnections, (Error*), (override));
  MOCK_METHOD(void,
              ParseWakeOnWiFiCapabilities,
              (const Nl80211Message&),
              (override));
  MOCK_METHOD(void,
              OnBeforeSuspend,
              (bool,
               const std::vector<ByteString>&,
               const ResultCallback&,
               const base::Closure&,
               const base::Closure&,
               bool,
               uint32_t),
              (override));
  MOCK_METHOD(void, OnAfterResume, (), (override));
  MOCK_METHOD(void,
              OnDarkResume,
              (bool,
               const std::vector<ByteString>&,
               const ResultCallback&,
               const base::Closure&,
               const InitiateScanCallback&,
               const base::Closure&),
              (override));
  MOCK_METHOD(void, OnConnectedAndReachable, (bool, uint32_t), (override));
  MOCK_METHOD(void, ReportConnectedToServiceAfterWake, (bool, int), (override));
  MOCK_METHOD(void,
              OnNoAutoConnectableServicesAfterScan,
              (const std::vector<ByteString>&,
               const base::Closure&,
               const InitiateScanCallback&),
              (override));
  MOCK_METHOD(void, OnScanStarted, (bool), (override));
  MOCK_METHOD(bool, InDarkResume, (), (override));
  MOCK_METHOD(void, OnWiphyIndexReceived, (uint32_t), (override));
};

}  // namespace shill

#endif  // SHILL_WIFI_MOCK_WAKE_ON_WIFI_H_
