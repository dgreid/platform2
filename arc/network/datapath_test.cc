// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/datapath.h"

#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/net_util.h"

using testing::_;
using testing::ElementsAre;
using testing::Return;
using testing::StrEq;

namespace arc_networkd {
namespace {

std::set<ioctl_req_t> ioctl_reqs;

// Capture all ioctls and succeed.
int ioctl_req_cap(int fd, ioctl_req_t req, ...) {
  ioctl_reqs.insert(req);
  return 0;
}

}  // namespace

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD6(AddInterfaceToContainer,
               int(const std::string& host_ifname,
                   const std::string& con_ifname,
                   uint32_t con_ipv4_addr,
                   uint32_t con_ipv4_prefix_len,
                   bool enable_multicast,
                   const std::string& con_pid));
  MOCK_METHOD1(WriteSentinelToContainer, int(const std::string& con_pid));
  MOCK_METHOD3(brctl,
               int(const std::string& cmd,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD4(chown,
               int(const std::string& uid,
                   const std::string& gid,
                   const std::string& file,
                   bool log_failures));
  MOCK_METHOD4(ip,
               int(const std::string& obj,
                   const std::string& cmd,
                   const std::vector<std::string>& args,
                   bool log_failures));
  MOCK_METHOD4(ip6,
               int(const std::string& obj,
                   const std::string& cmd,
                   const std::vector<std::string>& args,
                   bool log_failures));
  MOCK_METHOD3(iptables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD3(ip6tables,
               int(const std::string& table,
                   const std::vector<std::string>& argv,
                   bool log_failures));
  MOCK_METHOD2(modprobe_all,
               int(const std::vector<std::string>& modules, bool log_failures));
  MOCK_METHOD3(sysctl_w,
               int(const std::string& key,
                   const std::string& value,
                   bool log_failures));
};

TEST(DatapathTest, AddTAP) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(Ipv4Addr(100, 115, 92, 4), 30, base::DoNothing());
  auto addr = subnet.AllocateAtOffset(0);
  auto ifname = datapath.AddTAP("foo0", &mac, addr.get(), "");
  EXPECT_EQ(ifname, "foo0");
  std::set<ioctl_req_t> expected = {TUNSETIFF,      TUNSETPERSIST, SIOCSIFADDR,
                                    SIOCSIFNETMASK, SIOCSIFHWADDR, SIOCGIFFLAGS,
                                    SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddTAPWithOwner) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  MacAddress mac = {1, 2, 3, 4, 5, 6};
  Subnet subnet(Ipv4Addr(100, 115, 92, 4), 30, base::DoNothing());
  auto addr = subnet.AllocateAtOffset(0);
  auto ifname = datapath.AddTAP("foo0", &mac, addr.get(), "root");
  EXPECT_EQ(ifname, "foo0");
  std::set<ioctl_req_t> expected = {TUNSETIFF,    TUNSETPERSIST,  TUNSETOWNER,
                                    SIOCSIFADDR,  SIOCSIFNETMASK, SIOCSIFHWADDR,
                                    SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddTAPNoAddrs) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  auto ifname = datapath.AddTAP("foo0", nullptr, nullptr, "");
  EXPECT_EQ(ifname, "foo0");
  std::set<ioctl_req_t> expected = {TUNSETIFF, TUNSETPERSIST, SIOCGIFFLAGS,
                                    SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, RemoveTAP) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip(StrEq("tuntap"), StrEq("del"),
                         ElementsAre("foo0", "mode", "tap"), true));
  Datapath datapath(&runner);
  datapath.RemoveTAP("foo0");
}

TEST(DatapathTest, AddBridge) {
  MockProcessRunner runner;
  Datapath datapath(&runner);
  EXPECT_CALL(runner, brctl(StrEq("addbr"), ElementsAre("br"), true));
  EXPECT_CALL(
      runner,
      ip(StrEq("addr"), StrEq("add"),
         ElementsAre("1.1.1.1/30", "brd", "1.1.1.3", "dev", "br"), true));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "up"), true));
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-A", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1", "-w"),
                               true));
  datapath.AddBridge("br", Ipv4Addr(1, 1, 1, 1), 30);
}

TEST(DatapathTest, AddVirtualBridgedInterface) {
  MockProcessRunner runner;
  // RemoveInterface is run first
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("delete"),
                         ElementsAre("veth_foo"), false));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("add"),
                         ElementsAre("veth_foo", "type", "veth", "peer", "name",
                                     "peer_foo"),
                         true));
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("veth_foo", "up"), true));
  EXPECT_CALL(
      runner,
      ip(StrEq("link"), StrEq("set"),
         ElementsAre("dev", "peer_foo", "addr", "00:11:22", "down"), true));
  EXPECT_CALL(runner,
              brctl(StrEq("addif"), ElementsAre("brfoo", "veth_foo"), true));
  Datapath datapath(&runner);
  EXPECT_EQ(datapath.AddVirtualBridgedInterface("foo", "00:11:22", "brfoo"),
            "peer_foo");
}

TEST(DatapathTest, RemoveInterface) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("delete"), ElementsAre("foo"), false));
  Datapath datapath(&runner);
  datapath.RemoveInterface("foo");
}

TEST(DatapathTest, RemoveBridge) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("mangle"),
                               ElementsAre("-D", "PREROUTING", "-i", "br", "-j",
                                           "MARK", "--set-mark", "1", "-w"),
                               true));
  EXPECT_CALL(runner,
              ip(StrEq("link"), StrEq("set"), ElementsAre("br", "down"), true));
  EXPECT_CALL(runner, brctl(StrEq("delbr"), ElementsAre("br"), true));
  Datapath datapath(&runner);
  datapath.RemoveBridge("br");
}

TEST(DatapathTest, AddInterfaceToContainer) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip(StrEq("link"), StrEq("set"),
                         ElementsAre("src", "netns", "123"), true));
  EXPECT_CALL(runner, AddInterfaceToContainer(StrEq("src"), StrEq("dst"),
                                              Ipv4Addr(1, 1, 1, 1), 30, true,
                                              StrEq("123")));
  Datapath datapath(&runner);
  datapath.AddInterfaceToContainer(123, "src", "dst", Ipv4Addr(1, 1, 1, 1), 30,
                                   true);
}

TEST(DatapathTest, AddLegacyIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-N", "dnat_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "dnat_arc", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-N", "try_arc", "-w"), true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-A", "PREROUTING", "-m", "socket",
                                   "--nowildcard", "-j", "ACCEPT", "-w"),
                       true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-p", "tcp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-p", "udp",
                                           "-j", "try_arc", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddLegacyIPv4DNAT("1.2.3.4");
}

TEST(DatapathTest, RemoveLegacyIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-p", "udp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-p", "tcp",
                                           "-j", "try_arc", "-w"),
                               true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"),
                       ElementsAre("-D", "PREROUTING", "-m", "socket",
                                   "--nowildcard", "-j", "ACCEPT", "-w"),
                       true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-F", "try_arc", "-w"), true));
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-X", "try_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-F", "dnat_arc", "-w"), true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-X", "dnat_arc", "-w"), true));
  Datapath datapath(&runner);
  datapath.RemoveLegacyIPv4DNAT();
}

TEST(DatapathTest, AddLegacyIPv4InboundDNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "try_arc", "-i", "wlan0", "-j",
                                           "dnat_arc", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddLegacyIPv4InboundDNAT("wlan0");
}

TEST(DatapathTest, RemoveLegacyIPv4InboundDNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              iptables(StrEq("nat"), ElementsAre("-F", "try_arc", "-w"), true));
  Datapath datapath(&runner);
  datapath.RemoveLegacyIPv4InboundDNAT();
}

TEST(DatapathTest, AddInboundIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-A", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, RemoveInboundIPv4DNAT) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-m", "socket", "--nowildcard", "-j",
                                           "ACCEPT", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "tcp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  EXPECT_CALL(runner, iptables(StrEq("nat"),
                               ElementsAre("-D", "PREROUTING", "-i", "eth0",
                                           "-p", "udp", "-j", "DNAT",
                                           "--to-destination", "1.2.3.4", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.RemoveInboundIPv4DNAT("eth0", "1.2.3.4");
}

TEST(DatapathTest, AddOutboundIPv4) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-A", "FORWARD", "-o", "eth0", "-j",
                                           "ACCEPT", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.AddOutboundIPv4("eth0");
}

TEST(DatapathTest, RemoveInboundIPv4) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, iptables(StrEq("filter"),
                               ElementsAre("-D", "FORWARD", "-o", "eth0", "-j",
                                           "ACCEPT", "-w"),
                               true));
  Datapath datapath(&runner);
  datapath.RemoveOutboundIPv4("eth0");
}

TEST(DatapathTest, MaskInterfaceFlags) {
  MockProcessRunner runner;
  Datapath datapath(&runner, ioctl_req_cap);
  bool result = datapath.MaskInterfaceFlags("foo0", IFF_DEBUG);
  EXPECT_TRUE(result);
  std::set<ioctl_req_t> expected = {SIOCGIFFLAGS, SIOCSIFFLAGS};
  EXPECT_EQ(ioctl_reqs, expected);
  ioctl_reqs.clear();
}

TEST(DatapathTest, AddIPv6Forwarding) {
  MockProcessRunner runner;
  // Return 1 on iptables -C to simulate rule not existing case
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false))
      .WillOnce(Return(1));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-A", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true));
  Datapath datapath(&runner);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6ForwardingRuleExists) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                false));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-C", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                false));
  Datapath datapath(&runner);
  datapath.AddIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, RemoveIPv6Forwarding) {
  MockProcessRunner runner;
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "eth0", "-o",
                                            "arc_eth0", "-j", "ACCEPT", "-w"),
                                true));
  EXPECT_CALL(runner, ip6tables(StrEq("filter"),
                                ElementsAre("-D", "FORWARD", "-i", "arc_eth0",
                                            "-o", "eth0", "-j", "ACCEPT", "-w"),
                                true));
  Datapath datapath(&runner);
  datapath.RemoveIPv6Forwarding("eth0", "arc_eth0");
}

TEST(DatapathTest, AddIPv6HostRoute) {
  MockProcessRunner runner;
  EXPECT_CALL(runner,
              ip6(StrEq("route"), StrEq("replace"),
                  ElementsAre("2001:da8:e00::1234/128", "dev", "eth0"), true));
  Datapath datapath(&runner);
  datapath.AddIPv6HostRoute("eth0", "2001:da8:e00::1234", 128);
}

}  // namespace arc_networkd
