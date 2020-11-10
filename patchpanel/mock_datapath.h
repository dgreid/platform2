// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_DATAPATH_H_
#define PATCHPANEL_MOCK_DATAPATH_H_

#include <string>

#include <base/macros.h>

#include "patchpanel/datapath.h"
#include "patchpanel/firewall.h"
#include "patchpanel/minijailed_process_runner.h"

namespace patchpanel {

// ARC networking data path configuration utility.
class MockDatapath : public Datapath {
 public:
  explicit MockDatapath(MinijailedProcessRunner* runner, Firewall* firewall)
      : Datapath(runner, firewall) {}
  MockDatapath(const MockDatapath&) = delete;
  MockDatapath& operator=(const MockDatapath&) = delete;

  ~MockDatapath() = default;

  MOCK_METHOD0(Start, void());
  MOCK_METHOD0(Stop, void());
  MOCK_METHOD2(NetnsAttachName,
               bool(const std::string& netns_name, pid_t netns_pid));
  MOCK_METHOD1(NetnsDeleteName, bool(const std::string& netns_name));

  MOCK_METHOD3(AddBridge,
               bool(const std::string& ifname,
                    uint32_t ipv4_addr,
                    uint32_t prefix_len));
  MOCK_METHOD1(RemoveBridge, void(const std::string& ifname));
  MOCK_METHOD2(AddToBridge,
               bool(const std::string& br_ifname, const std::string& ifname));

  MOCK_METHOD4(AddTAP,
               std::string(const std::string& name,
                           const MacAddress* mac_addr,
                           const SubnetAddress* ipv4_addr,
                           const std::string& user));
  MOCK_METHOD8(ConnectVethPair,
               bool(pid_t pid,
                    const std::string& netns_name,
                    const std::string& veth_ifname,
                    const std::string& peer_ifname,
                    const MacAddress& remote_mac_addr,
                    uint32_t remote_ipv4_addr,
                    uint32_t remote_ipv4_prefix_len,
                    bool remote_multicast_flag));
  MOCK_METHOD1(RemoveInterface, void(const std::string& ifname));
  MOCK_METHOD4(StartRoutingDevice,
               void(const std::string& ext_ifname,
                    const std::string& int_ifname,
                    uint32_t int_ipv4_addr,
                    TrafficSource source));
  MOCK_METHOD4(StopRoutingDevice,
               void(const std::string& ext_ifname,
                    const std::string& int_ifname,
                    uint32_t int_ipv4_addr,
                    TrafficSource source));
  MOCK_METHOD3(MaskInterfaceFlags,
               bool(const std::string& ifname, uint16_t on, uint16_t off));
  MOCK_METHOD2(AddIPv6Forwarding,
               bool(const std::string& ifname1, const std::string& ifname2));
  MOCK_METHOD2(RemoveIPv6Forwarding,
               void(const std::string& ifname1, const std::string& ifname2));
  MOCK_METHOD3(AddIPv4Route, bool(uint32_t gw, uint32_t dst, uint32_t netmask));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_DATAPATH_H_
