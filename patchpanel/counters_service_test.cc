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
  312491 1767147156 rx_input_eth0  all  --  eth0   any     anywhere             anywhere
       0        0 rx_input_wlan0  all  --  wlan0  any     anywhere             anywhere

Chain FORWARD (policy ACCEPT 18194 packets, 133612816 bytes)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668 tx_fwd_eth0  all  --  any    eth0    anywhere             anywhere
   11683 65571148 rx_fwd_eth0  all  --  eth0   any     anywhere             anywhere
       0        0 tx_fwd_wlan0  all  --  any    wlan0   anywhere             anywhere
       0        0 rx_fwd_wlan0  all  --  wlan0  any     anywhere             anywhere

Chain OUTPUT (policy ACCEPT 4574 packets, 2900995 bytes)
    pkts      bytes target     prot opt in     out     source               destination

Chain POSTROUTING (policy ACCEPT 22811 packets, 136518827 bytes)
    pkts      bytes target     prot opt in     out     source               destination
  202160 1807550291 tx_postrt_eth0  all  --  any    eth0    anywhere             anywhere             owner socket exists
       2       96 tx_postrt_wlan0  all  --  any    wlan0   anywhere             anywhere             owner socket exists

Chain tx_fwd_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668            all  --  any    any     anywhere             anywhere

Chain tx_fwd_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
       0        0            all  --  any    any     anywhere             anywhere

Chain tx_postrt_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
  202160 1807550291            all  --  any    any     anywhere             anywhere

Chain tx_postrt_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
       2       96            all  --  any    any     anywhere             anywhere

Chain rx_fwd_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
   11683 65571148            all  --  any    any     anywhere             anywhere

Chain rx_fwd_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
       0        0            all  --  any    any     anywhere             anywhere

Chain rx_input_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
  312491 1767147156            all  --  any    any     anywhere             anywhere

Chain rx_input_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
       0        0            all  --  any    any     anywhere             anywhere
)";

// The expected counters for the above output. "* 2" because the same string
// will be returned for both iptables and ip6tables in the tests.
const Counter kCounter_eth0{(65571148 + 1767147156ULL) * 2 /*rx_bytes*/,
                            (11683 + 312491) * 2 /*rx_packets*/,
                            (68041668 + 1807550291ULL) * 2 /*tx_bytes*/,
                            (6511 + 202160) * 2 /*tx_packets*/};
const Counter kCounter_wlan0{0, 0, 96 * 2, 2 * 2};

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
    counters_svc_ =
        std::make_unique<CountersService>(fake_shill_client_.get(), &runner_);
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
  std::unique_ptr<FakeShillClient> fake_shill_client_;
  std::unique_ptr<CountersService> counters_svc_;
};

TEST_F(CountersServiceTest, OnNewDevice) {
  // Makes the check commands return 1 (not found).
  EXPECT_CALL(runner_, iptables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));

  // The following commands are expected when eth0 comes up.
  const std::vector<std::vector<std::string>> expected_calls{
      {"-N", "rx_input_eth0", "-w"},
      {"-A", "INPUT", "-i", "eth0", "-j", "rx_input_eth0", "-w"},
      {"-A", "rx_input_eth0", "-w"},

      {"-N", "rx_fwd_eth0", "-w"},
      {"-A", "FORWARD", "-i", "eth0", "-j", "rx_fwd_eth0", "-w"},
      {"-A", "rx_fwd_eth0", "-w"},

      {"-N", "tx_postrt_eth0", "-w"},
      {"-A", "POSTROUTING", "-o", "eth0", "-m", "owner", "--socket-exists",
       "-j", "tx_postrt_eth0", "-w"},
      {"-A", "tx_postrt_eth0", "-w"},

      {"-N", "tx_fwd_eth0", "-w"},
      {"-A", "FORWARD", "-o", "eth0", "-j", "tx_fwd_eth0", "-w"},
      {"-A", "tx_fwd_eth0", "-w"},
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
  // Makes the check commands return 0 (we already have these rules).
  EXPECT_CALL(runner_, iptables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(0));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(0));

  // Creating chains commands are expected but no more creating rules command
  // (with "-I" or "-A") should come.
  EXPECT_CALL(runner_, iptables(_, Contains("-N"), _, _)).Times(AnyNumber());
  EXPECT_CALL(runner_, ip6tables(_, Contains("-N"), _, _)).Times(AnyNumber());

  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
}

TEST_F(CountersServiceTest, ChainNameLength) {
  // Makes the check commands return 1 (not found).
  EXPECT_CALL(runner_, iptables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));
  EXPECT_CALL(runner_, ip6tables(_, Contains("-C"), _, _))
      .WillRepeatedly(Return(1));

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

  std::map<SourceDevice, Counter> expected{
      {{TrafficCounter::UNKNOWN, "eth0"}, kCounter_eth0},
      {{TrafficCounter::UNKNOWN, "wlan0"}, kCounter_wlan0},
  };

  EXPECT_THAT(actual, ContainerEq(expected));
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithFilter) {
  EXPECT_CALL(runner_, iptables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));
  EXPECT_CALL(runner_, ip6tables(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<3>(kIptablesOutput), Return(0)));

  // Only counters for eth0 should be returned. eth1 should be ignored.
  auto actual = counters_svc_->GetCounters({"eth0", "eth1"});

  std::map<SourceDevice, Counter> expected{
      {{TrafficCounter::UNKNOWN, "eth0"}, kCounter_eth0},
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
Chain tx_fwd_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668            all  --  any    any     anywhere             anywhere

Chain tx_fwd_wlan0 (1 references)
)";
  TestBadIptablesOutput(kBadOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithOnlyChainNameAndHeader) {
  const std::string kBadOutput = R"(
Chain tx_fwd_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668            all  --  any    any     anywhere             anywhere

Chain tx_fwd_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
)";
  TestBadIptablesOutput(kBadOutput);
}

TEST_F(CountersServiceTest, QueryTrafficCountersWithNotFinishedCountersLine) {
  const std::string kBadOutput = R"(
Chain tx_fwd_eth0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination
    6511 68041668            all  --  any    any     anywhere             anywhere

Chain tx_fwd_wlan0 (1 references)
    pkts      bytes target     prot opt in     out     source               destination    pkts      bytes target     prot opt in     out     source               destination
       0     )";
  TestBadIptablesOutput(kBadOutput);
}

}  // namespace
}  // namespace patchpanel
