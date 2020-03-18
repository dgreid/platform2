// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/port_tracker.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "permission_broker/mock_firewall.h"

using ::testing::_;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace permission_broker {

class MockPortTracker : public PortTracker {
 public:
  explicit MockPortTracker(Firewall* firewall)
      : PortTracker(nullptr, firewall) {}
  ~MockPortTracker() override = default;

  MOCK_METHOD(int, AddLifelineFd, (int), (override));
  MOCK_METHOD(bool, DeleteLifelineFd, (int), (override));
  MOCK_METHOD(void, CheckLifelineFds, (bool), (override));
  MOCK_METHOD(void, ScheduleLifelineCheck, (), (override));

  MOCK_METHOD(bool, InitializeEpollOnce, (), (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockPortTracker);
};

class PortTrackerTest : public testing::Test {
 public:
  PortTrackerTest() : port_tracker_{&mock_firewall_} {}
  ~PortTrackerTest() override = default;

 protected:
  void SetMockExpectations(MockFirewall* firewall, bool success) {
    // Empty criterion matches all commands.
    firewall->SetRunInMinijailFailCriterion(std::vector<std::string>(), true,
                                            success);
  }

  void SetMockExpectationsPerExecutable(MockFirewall* firewall,
                                        bool ip4_success,
                                        bool ip6_success) {
    if (!ip4_success)
      firewall->SetRunInMinijailFailCriterion(
          std::vector<std::string>({kIpTablesPath}), true /* repeat */,
          false /* omit_failure */);
    if (!ip6_success)
      firewall->SetRunInMinijailFailCriterion(
          std::vector<std::string>({kIp6TablesPath}), true, false);
  }

  MockFirewall mock_firewall_;
  MockPortTracker port_tracker_;

  uint16_t tcp_port = 8080;
  uint16_t udp_port = 5353;
  uint16_t reserved_port = 443;

  std::string interface = "interface";

  std::string arc_addr = "100.115.92.2";
  std::string crosvm_addr = "100.115.92.6";
  std::string pluginvm_addr = "100.115.93.10";
  std::string non_guest_addr = "192.168.1.128";
  std::string ipv4_any = "0.0.0.0";

  int dbus_fd = 3;     // First fd not std{in|out|err}. Doesn't get used at all.
  int tracked_fd = 4;  // Next "available" fd. Used only as a placeholder.

 private:
  DISALLOW_COPY_AND_ASSIGN(PortTrackerTest);
};

TEST_F(PortTrackerTest, AllowTcpPortAccess_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowUdpPortAccess_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowTcpPortAccess_Twice) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  EXPECT_CALL(port_tracker_, CheckLifelineFds(false));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));
  ASSERT_FALSE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowUdpPortAccess_Twice) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  EXPECT_CALL(port_tracker_, CheckLifelineFds(false));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));
  ASSERT_FALSE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowTcpPortAccess_FirewallFailure) {
  // Make 'iptables' fail.
  SetMockExpectations(&mock_firewall_, false /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowUdpPortAccess_FirewallFailure) {
  // Make 'iptables' fail.
  SetMockExpectations(&mock_firewall_, false /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowTcpPortAccess_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, AllowUdpPortAccess_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));
}

TEST_F(PortTrackerTest, RevokeTcpPortAccess_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.RevokeTcpPortAccess(tcp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, RevokeUdpPortAccess_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.RevokeUdpPortAccess(udp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, RevokeTcpPortAccess_FirewallFailure) {
  // Make plugging the firewall hole fail.
  mock_firewall_.SetRunInMinijailFailCriterion(
      std::vector<std::string>({"-D", "-p", "tcp"}), true /* repeat */,
      false /* omit_failure */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.RevokeTcpPortAccess(tcp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, RevokeUdpPortAccess_DbusFailure) {
  // Make plugging the firewall hole fail.
  mock_firewall_.SetRunInMinijailFailCriterion(
      std::vector<std::string>({"-D", "-p", "udp"}), true /* repeat */,
      false /* omit_failure */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.RevokeUdpPortAccess(udp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, RevokeTcpPortAccess_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, interface, dbus_fd));

  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(false));
  ASSERT_FALSE(port_tracker_.RevokeTcpPortAccess(tcp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, RevokeUdpPortAccess_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, interface, dbus_fd));

  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(false));
  ASSERT_FALSE(port_tracker_.RevokeUdpPortAccess(udp_port, interface));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, LockDownLoopbackTcpPort_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  ASSERT_TRUE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, LockDownLoopbackTcpPort_Twice) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(0));
  EXPECT_CALL(port_tracker_, CheckLifelineFds(false));
  ASSERT_TRUE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, LockDownLoopbackTcpPort_FirewallFailure) {
  // Make 'iptables' fail.
  SetMockExpectations(&mock_firewall_, false /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, LockDownLoopbackTcpPort_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, ReleaseLoopbackTcpPort_Success) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.ReleaseLoopbackTcpPort(tcp_port));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, ReleaseLoopbackTcpPort_FirewallFailure) {
  // Make releasing the lockdown fail.
  mock_firewall_.SetRunInMinijailFailCriterion(
      std::vector<std::string>({"-D", "-p", "tcp"}), true /* repeat */,
      false /* omit_failure */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.ReleaseLoopbackTcpPort(tcp_port));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, ReleaseLoopbackTcpPort_EpollFailure) {
  SetMockExpectations(&mock_firewall_, true /* success */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd))
      .WillOnce(Return(tracked_fd));
  ASSERT_TRUE(port_tracker_.LockDownLoopbackTcpPort(tcp_port, dbus_fd));

  // Make epoll(7) fail.
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(tracked_fd))
      .WillOnce(Return(false));
  ASSERT_FALSE(port_tracker_.ReleaseLoopbackTcpPort(tcp_port));
  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartPortForwarding_BaseSuccessCase) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      udp_port, "eth0", crosvm_addr, udp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(7));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "wlan0", pluginvm_addr, tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(8));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      udp_port, "wlan0", pluginvm_addr, udp_port, dbus_fd));

  ASSERT_TRUE(port_tracker_.HasActiveRules());
  ASSERT_EQ(8, mock_firewall_.CountActiveCommands());
}

TEST_F(PortTrackerTest, StartAdbPortForwarding_BaseSuccessCase) {
  SetMockExpectations(&mock_firewall_, true /* success */);
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(7));
  ASSERT_TRUE(port_tracker_.StartAdbPortForwarding("vmtap0", dbus_fd));

  ASSERT_TRUE(port_tracker_.HasActiveRules());
  ASSERT_EQ(2, mock_firewall_.CountActiveCommands());
}

TEST_F(PortTrackerTest, StartPortForwarding_LifelineFdFailure) {
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartAdbPortForwarding_LifelineFdFailure) {
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(-1));
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("vmtap0", dbus_fd));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartPortForwarding_IptablesFailure) {
  SetMockExpectations(&mock_firewall_, false /* failure */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(5)).WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(6)).WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartAdbPortForwarding_IptablesFailure) {
  SetMockExpectations(&mock_firewall_, false /* failure */);

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(7));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(7)).WillOnce(Return(true));
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("vmtap0", dbus_fd));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartPortForwarding_InputPortValidation) {
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      reserved_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      reserved_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.HasActiveRules());

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, reserved_port, dbus_fd));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, reserved_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.HasActiveRules());
  ASSERT_EQ(4, mock_firewall_.CountActiveCommands());
}

TEST_F(PortTrackerTest, StartPortForwarding_InputInterfaceValidation) {
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(tcp_port, "", crosvm_addr,
                                                    tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(tcp_port, "", crosvm_addr,
                                                    tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(tcp_port, "lo", crosvm_addr,
                                                    tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(tcp_port, "lo", crosvm_addr,
                                                    tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "iface0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "iface0", crosvm_addr, tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "ETH0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "WLAN0", crosvm_addr, tcp_port, dbus_fd));

  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth1", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "usb0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "wlan0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "mlan0", crosvm_addr, tcp_port, dbus_fd));

  ASSERT_TRUE(port_tracker_.HasActiveRules());
  ASSERT_EQ(10, mock_firewall_.CountActiveCommands());
}

TEST_F(PortTrackerTest, StartAdbPortForwarding_InputInterfaceValidation) {
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("", dbus_fd));
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("lo", dbus_fd));
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("iface0", dbus_fd));
  ASSERT_FALSE(port_tracker_.StartAdbPortForwarding("VMTAP0", dbus_fd));
  ASSERT_TRUE(port_tracker_.StartAdbPortForwarding("vmtap0", dbus_fd));
  ASSERT_TRUE(port_tracker_.StartAdbPortForwarding("vmtap1", dbus_fd));

  ASSERT_TRUE(port_tracker_.HasActiveRules());
  ASSERT_EQ(4, mock_firewall_.CountActiveCommands());
}

TEST_F(PortTrackerTest, StartPortForwarding_TargetIpAddressValidation) {
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", "not_an_ip", tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(tcp_port, "eth0", ipv4_any,
                                                    tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", non_guest_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", "2001:db8::1", tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(tcp_port, "eth0", ipv4_any,
                                                    tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", non_guest_addr, tcp_port, dbus_fd));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StopPortForwarding) {
  // Cannot stop before starting.
  ASSERT_FALSE(port_tracker_.StopTcpPortForwarding(tcp_port, "eth0"));
  ASSERT_FALSE(port_tracker_.StopUdpPortForwarding(tcp_port, "eth0"));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(5)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopTcpPortForwarding(tcp_port, "eth0"));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(6)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopUdpPortForwarding(tcp_port, "eth0"));

  // Cannot stop twice.
  ASSERT_FALSE(port_tracker_.StopTcpPortForwarding(tcp_port, "eth0"));
  ASSERT_FALSE(port_tracker_.StopUdpPortForwarding(tcp_port, "eth0"));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StopAdbPortForwarding) {
  // Cannot stop before starting.
  ASSERT_FALSE(port_tracker_.StopAdbPortForwarding("vmtap0"));

  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(7));
  ASSERT_TRUE(port_tracker_.StartAdbPortForwarding("vmtap0", dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(7)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopAdbPortForwarding("vmtap0"));

  // Cannot stop twice.
  ASSERT_FALSE(port_tracker_.StopAdbPortForwarding("vmtap0"));

  ASSERT_FALSE(port_tracker_.HasActiveRules());
}

TEST_F(PortTrackerTest, StartPortForwarding_PreventOverwrite) {
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  // Cannot overwrite a (protocol, port, interface) entry.
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(tcp_port, "eth0", arc_addr,
                                                    443, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(tcp_port, "eth0", arc_addr,
                                                    443, dbus_fd));

  EXPECT_CALL(port_tracker_, DeleteLifelineFd(5)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopTcpPortForwarding(tcp_port, "eth0"));
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(6)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopUdpPortForwarding(tcp_port, "eth0"));

  ASSERT_FALSE(port_tracker_.HasActiveRules());

  // Previous entry was deleted.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(tcp_port, "eth0", arc_addr,
                                                   443, dbus_fd));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(tcp_port, "eth0", arc_addr,
                                                   443, dbus_fd));

  ASSERT_TRUE(port_tracker_.HasActiveRules());

  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.StartUdpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
}

TEST_F(PortTrackerTest, StartPortForwarding_PreventForwardingOpenPort) {
  SetMockExpectations(&mock_firewall_, true /* success */);

  // Open TCP port, that port cannot be forwarded for the same interface.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(5));
  ASSERT_TRUE(port_tracker_.AllowTcpPortAccess(tcp_port, "eth0", dbus_fd));
  ASSERT_FALSE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  // Forward UDP port, that port cannot be opened for the same interface.
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(6));
  ASSERT_TRUE(port_tracker_.StartUdpPortForwarding(
      udp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));
  ASSERT_FALSE(port_tracker_.AllowUdpPortAccess(udp_port, "eth0", dbus_fd));

  // Close TCP port, that port can now be forwarded.
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(5)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.RevokeTcpPortAccess(tcp_port, "eth0"));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(7));
  ASSERT_TRUE(port_tracker_.StartTcpPortForwarding(
      tcp_port, "eth0", crosvm_addr, tcp_port, dbus_fd));

  // Stop forwarding UDP port, that port can now be opened.
  EXPECT_CALL(port_tracker_, DeleteLifelineFd(6)).WillOnce(Return(true));
  ASSERT_TRUE(port_tracker_.StopUdpPortForwarding(udp_port, "eth0"));
  EXPECT_CALL(port_tracker_, AddLifelineFd(dbus_fd)).WillOnce(Return(8));
  ASSERT_TRUE(port_tracker_.AllowUdpPortAccess(udp_port, "eth0", dbus_fd));
}
}  // namespace permission_broker
