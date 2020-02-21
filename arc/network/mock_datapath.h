// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_MOCK_DATAPATH_H_
#define ARC_NETWORK_MOCK_DATAPATH_H_

#include <string>

#include <base/macros.h>

#include "arc/network/datapath.h"
#include "arc/network/minijailed_process_runner.h"

namespace arc_networkd {

// ARC networking data path configuration utility.
class MockDatapath : public Datapath {
 public:
  explicit MockDatapath(MinijailedProcessRunner* runner) : Datapath(runner) {}
  ~MockDatapath() = default;

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
  MOCK_METHOD3(AddVirtualBridgedInterface,
               std::string(const std::string& ifname,
                           const std::string& mac_addr,
                           const std::string& br_ifname));
  MOCK_METHOD1(RemoveInterface, void(const std::string& ifname));
  MOCK_METHOD6(AddInterfaceToContainer,
               bool(int ns,
                    const std::string& src_ifname,
                    const std::string& dst_ifname,
                    const uint32_t dst_ipv4_addr,
                    const uint32_t dst_prefix_len,
                    bool fwd_multicast));
  MOCK_METHOD1(AddLegacyIPv4DNAT, bool(const std::string& ipv4_addr));
  MOCK_METHOD0(RemoveLegacyIPv4DNAT, void());
  MOCK_METHOD1(AddLegacyIPv4InboundDNAT, bool(const std::string& ifname));
  MOCK_METHOD0(RemoveLegacyIPv4InboundDNAT, void());
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

}  // namespace arc_networkd

#endif  // ARC_NETWORK_MOCK_DATAPATH_H_
