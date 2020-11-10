// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_PPP_DEVICE_H_
#define SHILL_MOCK_PPP_DEVICE_H_

#include <map>
#include <string>

#include <gmock/gmock.h>

#include "shill/ppp_device.h"

namespace shill {

class MockPPPDevice : public PPPDevice {
 public:
  MockPPPDevice(Manager* manager,
                const std::string& link_name,
                int interface_index);
  MockPPPDevice(const MockPPPDevice&) = delete;
  MockPPPDevice& operator=(const MockPPPDevice&) = delete;

  ~MockPPPDevice() override;

  MOCK_METHOD(void,
              Stop,
              (Error*, const EnabledStateChangedCallback&),
              (override));
  MOCK_METHOD(void, UpdateIPConfig, (const IPConfig::Properties&), (override));
  MOCK_METHOD(void, DropConnection, (), (override));
  MOCK_METHOD(void, SelectService, (const ServiceRefPtr&), (override));
  MOCK_METHOD(void, SetServiceState, (Service::ConnectState), (override));
  MOCK_METHOD(void, SetServiceFailure, (Service::ConnectFailure), (override));
  MOCK_METHOD(void,
              SetServiceFailureSilent,
              (Service::ConnectFailure),
              (override));
  MOCK_METHOD(void, SetEnabled, (bool), (override));
  MOCK_METHOD(void,
              UpdateIPConfigFromPPP,
              ((const std::map<std::string, std::string>&), bool),
              (override));
#ifndef DISABLE_DHCPV6
  MOCK_METHOD(bool, AcquireIPv6Config, (), (override));
#endif
};

}  // namespace shill

#endif  // SHILL_MOCK_PPP_DEVICE_H_
