// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_MOCK_MM1_PROXY_H_
#define SHILL_CELLULAR_MOCK_MM1_PROXY_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/cellular/mm1_proxy_interface.h"

namespace shill {
namespace mm1 {

class MockMm1Proxy : public Mm1ProxyInterface {
 public:
  MockMm1Proxy();
  ~MockMm1Proxy() override;
  MockMm1Proxy(const MockMm1Proxy&) = delete;
  MockMm1Proxy& operator=(const MockMm1Proxy&) = delete;

  // Inherited methods from ModemProxyInterface.
  MOCK_METHOD(void, ScanDevices, (const ResultCallback&, int), (override));
  MOCK_METHOD(void,
              SetLogging,
              (const std::string&, const ResultCallback&, int),
              (override));
  MOCK_METHOD(void,
              InhibitDevice,
              (const std::string&, bool, const ResultCallback&, int),
              (override));
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_CELLULAR_MOCK_MM1_PROXY_H_
