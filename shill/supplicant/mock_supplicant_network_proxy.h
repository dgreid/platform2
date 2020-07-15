// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_MOCK_SUPPLICANT_NETWORK_PROXY_H_
#define SHILL_SUPPLICANT_MOCK_SUPPLICANT_NETWORK_PROXY_H_

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/supplicant/supplicant_network_proxy_interface.h"

namespace shill {

class MockSupplicantNetworkProxy : public SupplicantNetworkProxyInterface {
 public:
  MockSupplicantNetworkProxy();
  ~MockSupplicantNetworkProxy() override;

  MOCK_METHOD(bool, SetEnabled, (bool), (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockSupplicantNetworkProxy);
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_MOCK_SUPPLICANT_NETWORK_PROXY_H_
