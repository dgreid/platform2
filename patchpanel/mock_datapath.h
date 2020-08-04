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
  ~MockDatapath() = default;

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
  MOCK_METHOD3(AddVirtualInterfacePair,
               bool(const std::string& netns_name,
                    const std::string& veth_ifname,
                    const std::string& peer_ifname));
  MOCK_METHOD2(ToggleInterface, bool(const std::string& ifname, bool up));
  MOCK_METHOD6(ConfigureInterface,
               bool(const std::string& ifname,
                    const MacAddress& mac_addr,
                    uint32_t addr,
                    uint32_t prefix_len,
                    bool up,
                    bool multicast));
  MOCK_METHOD1(RemoveInterface, void(const std::string& ifname));
  MOCK_METHOD2(AddInboundIPv4DNAT,
               bool(const std::string& ifname, const std::string& ipv4_addr));
  MOCK_METHOD2(RemoveInboundIPv4DNAT,
               void(const std::string& ifname, const std::string& ipv4_addr));
  MOCK_METHOD1(AddOutboundIPv4, bool(const std::string& ifname));
  MOCK_METHOD1(RemoveOutboundIPv4, void(const std::string& ifname));
  MOCK_METHOD3(MaskInterfaceFlags,
               bool(const std::string& ifname, uint16_t on, uint16_t off));
  MOCK_METHOD2(AddIPv6Forwarding,
               bool(const std::string& ifname1, const std::string& ifname2));
  MOCK_METHOD2(RemoveIPv6Forwarding,
               void(const std::string& ifname1, const std::string& ifname2));
  MOCK_METHOD3(AddIPv4Route, bool(uint32_t gw, uint32_t dst, uint32_t netmask));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDatapath);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_DATAPATH_H_
