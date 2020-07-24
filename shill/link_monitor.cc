// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/link_monitor.h"

#include <string>

#include <base/bind.h>

#include "shill/active_link_monitor.h"
#include "shill/connection.h"
#include "shill/device_info.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/net/shill_time.h"
#include "shill/passive_link_monitor.h"

using base::Bind;
using base::Unretained;
namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kLink;
static std::string ObjectID(const Connection* c) {
  return c->interface_name();
}
}  // namespace Logging

const int LinkMonitor::kFailureThreshold = ActiveLinkMonitor::kFailureThreshold;
const char LinkMonitor::kDefaultLinkMonitorTechnologies[] = "wifi";

LinkMonitor::LinkMonitor(const ConnectionRefPtr& connection,
                         EventDispatcher* dispatcher,
                         Metrics* metrics,
                         DeviceInfo* device_info,
                         const FailureCallback& failure_callback,
                         const GatewayChangeCallback& gateway_change_callback)
    : connection_(connection),
      dispatcher_(dispatcher),
      metrics_(metrics),
      failure_callback_(failure_callback),
      gateway_change_callback_(gateway_change_callback),
      active_link_monitor_(new ActiveLinkMonitor(
          connection,
          dispatcher,
          metrics,
          device_info,
          Bind(&LinkMonitor::OnActiveLinkMonitorFailure, Unretained(this)),
          Bind(&LinkMonitor::OnActiveLinkMonitorSuccess, Unretained(this)))),
      passive_link_monitor_(new PassiveLinkMonitor(
          connection,
          dispatcher,
          Bind(&LinkMonitor::OnPassiveLinkMonitorResultCallback,
               Unretained(this)))),
      time_(Time::GetInstance()) {}

LinkMonitor::~LinkMonitor() {
  Stop();
}

bool LinkMonitor::Start() {
  Stop();
  time_->GetTimeMonotonic(&started_monitoring_at_);
  // Start active link monitor.
  return active_link_monitor_->Start(
      ActiveLinkMonitor::kDefaultTestPeriodMilliseconds);
}

void LinkMonitor::Stop() {
  SLOG(connection_.get(), 2) << "In " << __func__ << ".";
  timerclear(&started_monitoring_at_);
  active_link_monitor_->Stop();
  passive_link_monitor_->Stop();
  gateway_mac_address_.Clear();
}

void LinkMonitor::OnAfterResume() {
  // Preserve gateway settings across resume.
  ByteString prior_gateway_mac_address(gateway_mac_address_);
  bool gateway_supports_unicast_arp =
      active_link_monitor_->gateway_supports_unicast_arp();
  Stop();
  gateway_mac_address_ = prior_gateway_mac_address;
  active_link_monitor_->set_gateway_mac_address(gateway_mac_address_);
  active_link_monitor_->set_gateway_supports_unicast_arp(
      gateway_supports_unicast_arp);

  active_link_monitor_->Start(ActiveLinkMonitor::kFastTestPeriodMilliseconds);
}

int LinkMonitor::GetResponseTimeMilliseconds() const {
  return active_link_monitor_->GetResponseTimeMilliseconds();
}

bool LinkMonitor::IsGatewayFound() const {
  return !gateway_mac_address_.IsZero();
}

void LinkMonitor::OnActiveLinkMonitorFailure(
    Metrics::LinkMonitorFailure failure,
    int broadcast_failure_count,
    int unicast_failure_count) {
  failure_callback_.Run();

  struct timeval now, elapsed_time;
  time_->GetTimeMonotonic(&now);
  timersub(&now, &started_monitoring_at_, &elapsed_time);

  metrics_->NotifyLinkMonitorFailure(
      connection_->technology(), failure, elapsed_time.tv_sec,
      broadcast_failure_count, unicast_failure_count);

  Stop();
}

void LinkMonitor::OnActiveLinkMonitorSuccess() {
  if (!gateway_mac_address_.Equals(
          active_link_monitor_->gateway_mac_address())) {
    gateway_mac_address_ = active_link_monitor_->gateway_mac_address();
    // Notify device of the new gateway mac address.
    gateway_change_callback_.Run();
  }

  // Start passive link monitoring.
  passive_link_monitor_->Start(PassiveLinkMonitor::kDefaultMonitorCycles);
}

void LinkMonitor::OnPassiveLinkMonitorResultCallback(bool status) {
  // TODO(zqiu): Add metrics for tracking passive link monitor results.

  // Start active monitor
  active_link_monitor_->Start(
      ActiveLinkMonitor::kDefaultTestPeriodMilliseconds);
}

}  // namespace shill
