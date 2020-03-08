// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_TRAFFIC_FORWARDER_H_
#define ARC_NETWORK_TRAFFIC_FORWARDER_H_

#include <string>

namespace arc_networkd {

// Interface to encapsulate traffic forwarding behaviors so individual services
// are not exposed to dependents.
class TrafficForwarder {
 public:
  virtual ~TrafficForwarder() = default;

  virtual void StartForwarding(const std::string& ifname_physical,
                               const std::string& ifname_virtual,
                               uint32_t ipv4_addr_virtual,
                               bool ipv6,
                               bool multicast) = 0;

  virtual void StopForwarding(const std::string& ifname_physical,
                              const std::string& ifname_virtual,
                              bool ipv6,
                              bool multicast) = 0;

 protected:
  TrafficForwarder() = default;
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_TRAFFIC_FORWARDER_H_
