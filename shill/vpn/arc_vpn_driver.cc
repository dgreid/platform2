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

namespace {

const char* const kDnsAndRoutingProperties[] = {
    kDomainNameProperty,     kNameServersProperty,    kSearchDomainsProperty,
    kIncludedRoutesProperty, kExcludedRoutesProperty,
};

}  // namespace

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

ArcVpnDriver::~ArcVpnDriver() {
  Cleanup();
}

void ArcVpnDriver::Cleanup() {
  if (device_) {
    device_->DropConnection();
    device_->SetEnabled(false);
    device_ = nullptr;
  }

  if (service()) {
    service()->SetState(Service::kStateIdle);
    set_service(nullptr);
  }
}

void ArcVpnDriver::Connect(const VPNServiceRefPtr& service, Error* error) {
  SLOG(this, 2) << __func__;

  device_ = manager()->vpn_provider()->arc_device();
  if (!device_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotFound,
                          "arc_device is not found");
    return;
  }

  set_service(service);
  // This sets the has_ever_connected flag.
  service->SetState(Service::kStateConnected);

  IPConfig::Properties ip_properties;
  if (args()->Lookup<std::string>(kArcVpnTunnelChromeProperty, "false") !=
      "true") {
    // If Chrome tunneling is disabled, traffic will not be passed through
    // this interface, but users will still be able to see VPN status
    // and disconnect the VPN through the UI.  In this case the IP address
    // and gateway should still be reflected in the properties, but the
    // DNS and routing information should be zapped so that Chrome
    // traffic falls through to the next highest service.
    Error error;
    std::string prefix(StaticIPParameters::kConfigKeyPrefix);
    for (const auto& property : kDnsAndRoutingProperties) {
      service->mutable_store()->ClearProperty(prefix + property, &error);
    }
    if (!error.IsSuccess()) {
      LOG(ERROR) << "Unable to clear VPN IP properties: " << error.message();
    }
  } else {
    // IPv6 is not currently supported.  If the VPN is enabled, block all
    // IPv6 traffic so there is no "leak" past the VPN.
    ip_properties.blackhole_ipv6 = true;
  }

  // This will always create the per-device routing table, but it might
  // not have any routes if |ip_properties.routes| is empty.
  manager()->vpn_provider()->SetDefaultRoutingPolicy(&ip_properties);
  // VPNProvider includes the arc device in the list of allowed iifs. This
  // would create a routing loop for ARC N traffic when an ARC VPN is used.
  //
  // TODO - This should be removed after ARC N is deprecated OR after we have
  // explicit linking between virtual interfaces and the "lower" interface that
  // will carry its traffic.
  base::Erase(ip_properties.allowed_iifs, device_->link_name());

  ip_properties.default_route = false;

  device_->SetEnabled(true);
  device_->SelectService(service);

  // Device::OnIPConfigUpdated() will apply the StaticIPConfig properties.
  device_->UpdateIPConfig(ip_properties);
  device_->SetLooseRouting(true);

  service->SetState(Service::kStateOnline);

  metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver, Metrics::kVpnDriverArc,
                           Metrics::kMetricVpnDriverMax);
}

void ArcVpnDriver::Disconnect() {
  SLOG(this, 2) << __func__;
  Cleanup();
}

std::string ArcVpnDriver::GetProviderType() const {
  return std::string(kProviderArcVpn);
}

VPNDriver::IfType ArcVpnDriver::GetIfType() const {
  return kDriverManaged;
}

}  // namespace shill
