// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_ARC_VPN_DRIVER_H_
#define SHILL_VPN_ARC_VPN_DRIVER_H_

#include <string>

#include <base/callback.h>
#include <gtest/gtest_prod.h>

#include "shill/device_info.h"
#include "shill/error.h"
#include "shill/ipconfig.h"
#include "shill/refptr_types.h"
#include "shill/service.h"
#include "shill/virtual_device.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class ArcVpnDriver : public VPNDriver {
 public:
  ArcVpnDriver(Manager* manager, ProcessManager* process_manager);
  ~ArcVpnDriver() override = default;

  std::string GetProviderType() const override;
  IfType GetIfType() const override;

  void ConnectAsync(const VPNService::DriverEventCallback& callback) override;
  void Disconnect() override;
  IPConfig::Properties GetIPProperties() const override;

 private:
  friend class ArcVpnDriverTest;

  static const Property kProperties[];

  DISALLOW_COPY_AND_ASSIGN(ArcVpnDriver);
};

}  // namespace shill

#endif  // SHILL_VPN_ARC_VPN_DRIVER_H_
