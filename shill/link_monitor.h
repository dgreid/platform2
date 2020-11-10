// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_LINK_MONITOR_H_
#define SHILL_LINK_MONITOR_H_

#include <time.h>

#include <memory>

#include <base/callback.h>
#include <base/cancelable_callback.h>

#include "shill/metrics.h"
#include "shill/net/byte_string.h"
#include "shill/refptr_types.h"

namespace shill {

class ActiveLinkMonitor;
class DeviceInfo;
class EventDispatcher;
class PassiveLinkMonitor;
class Time;

class LinkMonitor {
 public:
  using FailureCallback = base::Closure;
  using GatewayChangeCallback = base::Closure;

  // The default list of technologies for which link monitoring is enabled.
  // Needed by DefaultProfile.
  static const char kDefaultLinkMonitorTechnologies[];

  // Failure threshold count used by ActiveLinkMonitor.  Needed by Metrics.
  static const int kFailureThreshold;

  LinkMonitor(const ConnectionRefPtr& connection,
              EventDispatcher* dispatcher,  // Owned by caller; can't be NULL.
              Metrics* metrics,  // Owned by caller; must not be NULL.
              DeviceInfo* device_info,
              const FailureCallback& failure_callback,
              const GatewayChangeCallback& gateway_change_callback);
  LinkMonitor(const LinkMonitor&) = delete;
  LinkMonitor& operator=(const LinkMonitor&) = delete;
  virtual ~LinkMonitor();

  // Starts link-monitoring on the selected connection.  Returns
  // true if successful, false otherwise.
  virtual bool Start();
  // Stop link-monitoring on the selected connection. Clears any
  // accumulated statistics.
  virtual void Stop();

  // Inform LinkMonitor that the system is resuming from sleep.
  // LinkMonitor will immediately start the ActiveLinkMonitor, using a lower
  // timeout than normal.
  virtual void OnAfterResume();

  // Return modified cumulative average of the gateway ARP response
  // time.  Returns zero if no samples are available.  For each
  // missed ARP response, the sample is assumed to be the full
  // test period.
  virtual int GetResponseTimeMilliseconds() const;

  // Returns true if the LinkMonitor was ever able to find the default
  // gateway via broadcast ARP.
  virtual bool IsGatewayFound() const;

  const ByteString& gateway_mac_address() const { return gateway_mac_address_; }

 private:
  friend class LinkMonitorTest;

  void OnActiveLinkMonitorFailure(Metrics::LinkMonitorFailure failure,
                                  int broadcast_failure_count,
                                  int unicast_failure_count);
  void OnActiveLinkMonitorSuccess();
  void OnPassiveLinkMonitorResultCallback(bool status);

  // The connection on which to perform link monitoring.
  ConnectionRefPtr connection_;
  // Dispatcher on which to create delayed tasks.
  EventDispatcher* dispatcher_;
  // Metrics instance on which to post performance results.
  Metrics* metrics_;
  // Failure callback method to call if LinkMonitor fails.
  FailureCallback failure_callback_;
  // Callback method to call if gateway mac address changes.
  GatewayChangeCallback gateway_change_callback_;
  std::unique_ptr<ActiveLinkMonitor> active_link_monitor_;
  std::unique_ptr<PassiveLinkMonitor> passive_link_monitor_;
  // The MAC address of the default gateway.
  ByteString gateway_mac_address_;
  // The time at which the link monitor started.
  struct timeval started_monitoring_at_;
  // Time instance for performing GetTimeMonotonic().
  Time* time_;
};

}  // namespace shill

#endif  // SHILL_LINK_MONITOR_H_
