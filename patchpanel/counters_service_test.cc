// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/counters_service.h"

#include <memory>
#include <net/if.h>
#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"
#include "patchpanel/mock_firewall.h"

namespace patchpanel {

using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::DoAll;
using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::Lt;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SizeIs;

using Counter = CountersService::Counter;
using SourceDevice = CountersService::SourceDevice;

// The following two functions should be put outside the anounymous namespace
// otherwise they could not be found in the tests.
std::ostream& operator<<(std::ostream& os, const Counter& counter) {
  os << "rx_bytes:" << counter.rx_bytes << ", rx_packets:" << counter.rx_packets
     << ", tx_bytes:" << counter.tx_bytes
     << ", tx_packets:" << counter.tx_packets;
  return os;
}

bool operator==(const CountersService::Counter lhs,
                const CountersService::Counter rhs) {
  return lhs.rx_bytes == rhs.rx_bytes && lhs.rx_packets == rhs.rx_packets &&
         lhs.tx_bytes == rhs.tx_bytes && lhs.tx_packets == rhs.tx_packets;
}

namespace {
// The following string is copied from the real output of iptables v1.6.2 by
// `iptables -t mangle -L -x -v`. This output contains all the accounting
// chains/rules for eth0 and wlan0.
// TODO(jiejiang): presubmit checker is complaining about the line length for
// this (and the other raw strings in this file). Find a way to make it happy.
const char kIptablesOutput[] = R"(
Chain PREROUTING (policy ACCEPT 22785 packets, 136093545 bytes)
    pkts      bytes target     prot opt in     out     source               destination
      18     2196 MARK       all  --  arcbr0 any     anywhere             anywhere             MARK set 0x1
       0        0 MARK       all  --  vmtap+ any     anywhere             anywhere             MARK set 0x1
    6526 68051766 MARK       all  --  arc_eth0 any     anywhere             anywhere             MARK set 0x1
       9     1104 MARK       all  --  arc_wlan0 any     anywhere             anywhere             MARK set 0x1

Chain INPUT (policy ACCEPT 4421 packets, 2461233 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  312491 1767147156 rx_eth0  all  --  eth0   any     anywhere             anywhere
       0        0 rx_wlan0  all  --  wlan0  any     anywhere             anywhere

Chain FORWARD (policy ACCEPT 18194 packets, 133612816 bytes)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 tx_eth0  all  --  any    eth0    anywhere             anywhere
   11683 65571148 rx_eth0  all  --  eth0   any     anywhere             anywhere

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 22811 packets, 136518827 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  202160 1807550291 tx_eth0  all  --  any    eth0    anywhere             anywhere             owner socket exists
       2       96 tx_wlan0  all  --  any    wlan0   anywhere             anywhere             owner socket exists

Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    1366   244427 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x200/0x3f00
      20     1670 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x300/0x3f00
     550   138402 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x400/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x500/0x3f00
    5374   876172 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2000/0x3f00
      39     2690 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2400/0x3f00

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
     310    57004 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x200/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x300/0x3f00
      24     2801 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x400/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x500/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2400/0x3f00

Chain rx_eth0 (2 references)
 pkts bytes target     prot opt in     out     source               destination
   73 11938 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x100/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x200/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x300/0x3f00
    5   694 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x400/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x500/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2000/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2100/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2200/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2300/0x3f00
    0     0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2400/0x3f00

Chain rx_wlan0 (2 references)
    pkts      bytes target     prot opt in     out     source               destination
     153    28098 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x200/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x300/0x3f00
       6      840 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x400/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x500/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2000/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2100/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2200/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2300/0x3f00
       0        0 RETURN     all  --  any    any     anywhere             anywhere             mark match 0x2400/0x3f00
)";

class MockProcessRunner : public MinijailedProcessRunner {
 public:
  MockProcessRunner() = default;
  ~MockProcessRunner() = default;

  MOCK_METHOD(int,
              iptables,
              (const std::string& table,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
  MOCK_METHOD(int,
              ip6tables,
              (const std::string& table,
               const std::vector<std::string>& argv,
               bool log_failures,
               std::string* output),
              (override));
};

class CountersServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_shill_client_ = shill_helper_.FakeClient();
    datapath_ = std::make_unique<Datapath>(&runner_, &firewall_);
    counters_svc_ = std::make_unique<CountersService>(
        fake_shill_client_.get(), datapath_.get(), &runner_);
  }

  // Makes `iptables` returning a bad |output|. Expects an empty map from
  // GetCounters().
  void TestBadIptablesOutput(const std::string& output) {
    EXPECT_CALL(runner_, iptables(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<3>(output), Return(0)));
    EXPECT_CALL(runner_, ip6tables(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));

    auto actual = counters_svc_->GetCounters({});
    std::map<SourceDevice, Counter> expected;

    EXPECT_THAT(actual, ContainerEq(expected));
  }

  // Makes `ip6tables` returning a bad |output|. Expects an empty map from
  // GetCounters().
  void TestBadIp6tablesOutput(const std::string& output) {
    EXPECT_CALL(runner_, iptables(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));
    EXPECT_CALL(runner_, ip6tables(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgPointee<3>(output), Return(0)));

    auto actual = counters_svc_->GetCounters({});
    std::map<SourceDevice, Counter> expected;

    EXPECT_THAT(actual, ContainerEq(expected));
  }

  FakeShillClientHelper shill_helper_;
  MockProcessRunner runner_;
  MockFirewall firewall_;
  std::unique_ptr<FakeShillClient> fake_shill_client_;
  std::unique_ptr<Datapath> datapath_;
  std::unique_ptr<CountersService> counters_svc_;
};

TEST_F(CountersServiceTest, OnNewDevice) {
  // The following commands are expected when eth0 comes up.
  const std::vector<std::vector<std::string>> expected_calls{
      {"-N", "rx_eth0", "-w"},
      {"-N", "tx_eth0", "-w"},
      {"-A", "INPUT", "-i", "eth0", "-j", "rx_eth0", "-w"},
      {"-A", "FORWARD", "-i", "eth0", "-j", "rx_eth0", "-w"},
      {"-A", "POSTROUTING", "-o", "eth0", "-j", "tx_eth0", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00000100/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00000200/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00000300/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00000400/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00000500/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00002000/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00002100/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00002200/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00002300/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "tx_eth0", "-m", "mark", "--mark", "0x00002400/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00000100/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00000200/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00000300/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00000400/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00000500/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00002000/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00002100/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00002200/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00002300/0x00003f00", "-j",
       "RETURN", "-w"},
      {"-A", "rx_eth0", "-m", "mark", "--mark", "0x00002400/0x00003f00", "-j",
       "RETURN", "-w"},
  };

  for (const auto& rule : expected_calls) {
    EXPECT_CALL(runner_, iptables("mangle", ElementsAreArray(rule), _, _));
    EXPECT_CALL(runner_, ip6tables("mangle", ElementsAreArray(rule), _, _));
  }

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

TEST_F(CountersServiceTest, OnSameDeviceAppearAgain) {
  // Makes the chain creation commands return false (we already have these
  // rules).
  EXPECT_CALL(runner_, iptables(_, Contains("-N"), _, _))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-N"), _, _))
      .WillRepeatedly(Return(1));

  // Creating chains commands are expected but no more creating rules command
  // (with "-I" or "-A") should come.
  EXPECT_CALL(runner_, iptables(_, Contains("-A"), _, _)).Times(0);
  EXPECT_CALL(runner_, ip6tables(_, Contains("-A"), _, _)).Times(0);
  EXPECT_CALL(runner_, iptables(_, Contains("-I"), _, _)).Times(0);
  EXPECT_CALL(runner_, ip6tables(_, Contains("-I"), _, _)).Times(0);

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

TEST_F(CountersServiceTest, ChainNameLength) {
  // The name of a new chain must be shorter than 29 characters, otherwise
  // iptables will reject the request. Uses Each() here for simplicity since no
  // other params could be longer than 29 for now.
  static constexpr int kMaxChainNameLength = 29;
  EXPECT_CALL(runner_, iptables(_, Each(SizeIs(Lt(kMaxChainNameLength))), _, _))
      .Times(AnyNumber());
  EXPECT_CALL(runner_,
              ip6tables(_, Each(SizeIs(Lt(kMaxChainNameLength))), _, _))
      .Times(AnyNumber());

  static const std::string kLongInterfaceName(IFNAMSIZ, 'a');
  std::vector<dbus::ObjectPath> devices = {
      dbus::ObjectPath("/device/" + kLongInterfaceName)};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

TEST_F(CountersServiceTest, QueryTrafficCounters) {
  EXPECT_CALL(runner_, iptables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));
  EXPECT_CALL(runner_, ip6tables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));

  auto actual = counters_svc_->GetCounters({});

  // The expected counters for eth0 and wlan0. All values are doubled because
  // the same output will be returned for both iptables and ip6tables in the
  // tests.
  std::map<SourceDevice, Counter> expected{
      {{TrafficCounter::CHROME, "eth0"},
       {23876 /*rx_bytes*/, 146 /*rx_packets*/, 488854 /*tx_bytes*/,
        2732 /*tx_packets*/}},
      {{TrafficCounter::UPDATE_ENGINE, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 3340 /*tx_bytes*/,
        40 /*tx_packets*/}},
      {{TrafficCounter::SYSTEM, "eth0"},
       {1388 /*rx_bytes*/, 10 /*rx_packets*/, 276804 /*tx_bytes*/,
        1100 /*tx_packets*/}},
      {{TrafficCounter::ARC, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1752344 /*tx_bytes*/,
        10748 /*tx_packets*/}},
      {{TrafficCounter::CROSVM, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 5380 /*tx_bytes*/,
        78 /*tx_packets*/}},
      {{TrafficCounter::CHROME, "wlan0"},
       {56196 /*rx_bytes*/, 306 /*rx_packets*/, 114008 /*tx_bytes*/,
        620 /*tx_packets*/}},
      {{TrafficCounter::SYSTEM, "wlan0"},
       {1680 /*rx_bytes*/, 12 /*rx_packets*/, 5602 /*tx_bytes*/,
        48 /*tx_packets*/}}};

  EXPECT_THAT(actual, ContainerEq(expected));
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithFilter) {
  EXPECT_CALL(runner_, iptables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));
  EXPECT_CALL(runner_, ip6tables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));

  // Only counters for eth0 should be returned. eth1 should be ignored.
  auto actual = counters_svc_->GetCounters({"eth0", "eth1"});

  // The expected counters for eth0. All values are doubled because
  // the same output will be returned for both iptables and ip6tables in the
  // tests.
  std::map<SourceDevice, Counter> expected{
      {{TrafficCounter::CHROME, "eth0"},
       {23876 /*rx_bytes*/, 146 /*rx_packets*/, 488854 /*tx_bytes*/,
        2732 /*tx_packets*/}},
      {{TrafficCounter::UPDATE_ENGINE, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 3340 /*tx_bytes*/,
        40 /*tx_packets*/}},
      {{TrafficCounter::SYSTEM, "eth0"},
       {1388 /*rx_bytes*/, 10 /*rx_packets*/, 276804 /*tx_bytes*/,
        1100 /*tx_packets*/}},
      {{TrafficCounter::ARC, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 1752344 /*tx_bytes*/,
        10748 /*tx_packets*/}},
      {{TrafficCounter::CROSVM, "eth0"},
       {0 /*rx_bytes*/, 0 /*rx_packets*/, 5380 /*tx_bytes*/,
        78 /*tx_packets*/}},
  };

  EXPECT_THAT(actual, ContainerEq(expected));
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithEmptyIPv4Output) {
  const std::string kEmptyOutput = "";
  TestBadIptablesOutput(kEmptyOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithEmptyIPv6Output) {
  const std::string kEmptyOutput = "";
  TestBadIp6tablesOutput(kEmptyOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithOnlyChainName) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  any    any     anywhere             anywhere

Chain tx_wlan0 (1 references)
)";
  TestBadIptablesOutput(kBadOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithOnlyChainNameAndHeader) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  any    any     anywhere             anywhere

Chain tx_fwd_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
)";
  TestBadIptablesOutput(kBadOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithNotFinishedCountersLine) {
  const std::string kBadOutput = R"(
Chain tx_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 RETURN    all  --  any    any     anywhere             anywhere

Chain tx_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination    pkts      bytes target     prot opt in     out     source               destination
       0     )";
  TestBadIptablesOutput(kBadOutput);
}

}  // namespace
}  // namespace patchpanel
