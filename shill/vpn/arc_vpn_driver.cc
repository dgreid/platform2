// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/arc_vpn_driver.h"

#include <fcntl.h>
#include <unistd.h>

#include <utility>

#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/static_ip_parameters.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_service.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const ArcVpnDriver* v) {
  return "(arc_vpn_driver)";
}
}  // namespace Logging

const VPNDriver::Property ArcVpnDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kArcVpnTunnelChromeProperty, 0}};

ArcVpnDriver::ArcVpnDriver(Manager* manager, ProcessManager* process_manager)
    : VPNDriver(
          manager, process_manager, kProperties, base::size(kProperties)) {}

void ArcVpnDriver::Connect(const VPNServiceRefPtr& service, Error* error) {
  SLOG(this, 2) << __func__;
  LOG(DFATAL) << "Not implemented";
}

void ArcVpnDriver::ConnectAsync(
    const VPNService::DriverEventCallback& callback) {
  SLOG(this, 2) << __func__;
  // Nothing to do here since ARC already finish connecting to VPN
  // before Chrome calls Service::OnConnect. Just return success.
  metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver, Metrics::kVpnDriverArc,
                           Metrics::kMetricVpnDriverMax);
  dispatcher()->PostTask(
      FROM_HERE,
      base::Bind(std::move(callback), VPNService::kEventConnectionSuccess));
}

void ArcVpnDriver::Disconnect() {
  SLOG(this, 2) << __func__;
}

IPConfig::Properties ArcVpnDriver::GetIPProperties() const {
  SLOG(this, 2) << __func__;
  // Currently L3 settings for ARC VPN are set from Chrome as
  // StaticIPProperty before connecting, so this will be mostly empty.
  return IPConfig::Properties();
}

std::string ArcVpnDriver::GetProviderType() const {
  return std::string(kProviderArcVpn);
}

VPNDriver::IfType ArcVpnDriver::GetIfType() const {
  return kArcBridge;
}

}  // namespace shill
