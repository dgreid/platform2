// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/network_monitor_service.h"

#include <memory>
#include <linux/rtnetlink.h>

#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>

#include "patchpanel/fake_shill_client.h"
#include "shill/net/mock_rtnl_handler.h"

namespace patchpanel {

namespace {
constexpr int kTestInterfaceIndex = 1;
constexpr char kTestInterfaceName[] = "wlan0";

MATCHER_P(IsNeighborGetMessage, address, "") {
  if (!(arg->type() == shill::RTNLMessage::kTypeNeighbor &&
        arg->flags() == NLM_F_REQUEST &&
        arg->mode() == shill::RTNLMessage::kModeGet &&
        arg->interface_index() == kTestInterfaceIndex &&
        arg->HasAttribute(NDA_DST)))
    return false;

  shill::IPAddress msg_address(arg->family(), arg->GetAttribute(NDA_DST));
  return msg_address == shill::IPAddress(address);
}

MATCHER_P(IsNeighborProbeMessage, address, "") {
  if (!(arg->type() == shill::RTNLMessage::kTypeNeighbor &&
        arg->flags() == NLM_F_REQUEST | NLM_F_REPLACE &&
        arg->mode() == shill::RTNLMessage::kModeAdd &&
        arg->neighbor_status().state == NUD_PROBE &&
        arg->interface_index() == kTestInterfaceIndex &&
        arg->HasAttribute(NDA_DST)))
    return false;

  shill::IPAddress msg_address(arg->family(), arg->GetAttribute(NDA_DST));
  return msg_address == shill::IPAddress(address);
}

shill::RTNLMessage* CreateIncomingRTNLMessage(
    const shill::RTNLMessage::Mode mode,
    const std::string& address,
    uint16_t nud_state) {
  auto* rtnl_response = new shill::RTNLMessage(
      shill::RTNLMessage::kTypeNeighbor, mode, 0, 0, 0, kTestInterfaceIndex,
      shill::IPAddress::kFamilyIPv4);

  rtnl_response->SetAttribute(NDA_DST, shill::IPAddress(address).address());
  if (mode == shill::RTNLMessage::kModeAdd) {
    rtnl_response->set_neighbor_status(
        shill::RTNLMessage::NeighborStatus(nud_state, 0, 0));
    rtnl_response->SetAttribute(
        NDA_LLADDR, shill::ByteString(std::vector<uint8_t>{1, 2, 3, 4, 5, 6}));
  }

  return rtnl_response;
}

shill::RTNLMessage* CreateNUDStateChangedMessage(const std::string& address,
                                                 uint16_t nud_state) {
  return CreateIncomingRTNLMessage(shill::RTNLMessage::kModeAdd, address,
                                   nud_state);
}

shill::RTNLMessage* CreateNeighborDeletedMessage(const std::string& address) {
  return CreateIncomingRTNLMessage(shill::RTNLMessage::kModeDelete, address, 0);
}

}  // namespace

class NeighborLinkMonitorTest : public testing::Test {
 protected:
  void SetUp() override {
    mock_rtnl_handler_ = std::make_unique<shill::MockRTNLHandler>();
    link_monitor_ = std::make_unique<NeighborLinkMonitor>(
        kTestInterfaceIndex, kTestInterfaceName, mock_rtnl_handler_.get());
  }

  void TearDown() override {
    // We should make sure |mock_rtnl_handler_| is valid during the life time of
    // |link_monitor_|.
    link_monitor_ = nullptr;
    mock_rtnl_handler_ = nullptr;
  }

  void FastForwardOneActiveProbeInterval() {
    task_environment_.FastForwardBy(NeighborLinkMonitor::kActiveProbeInterval);
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<shill::MockRTNLHandler> mock_rtnl_handler_;
  std::unique_ptr<NeighborLinkMonitor> link_monitor_;
};

TEST_F(NeighborLinkMonitorTest, SendNeighborGetMessageOnIPConfigChanged) {
  ShillClient::IPConfig ipconfig;
  ipconfig.ipv4_address = "1.2.3.4";
  ipconfig.ipv4_gateway = "1.2.3.5";
  ipconfig.ipv4_prefix_length = 24;
  // The second dns address is not in this subnet, and should be ignored by the
  // link monitor.
  ipconfig.ipv4_dns_addresses = {"1.2.3.6", "4.3.2.1"};

  // On ipconfig changed, the link monitor should send a get request for each
  // watching address, to fetch their current NUD state.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("1.2.3.6"), _))
      .WillOnce(Return(true));

  link_monitor_->OnIPConfigChanged(ipconfig);
}

TEST_F(NeighborLinkMonitorTest, WatchLinkLocalIPv6DNSServerAddress) {
  ShillClient::IPConfig ipconfig;
  ipconfig.ipv6_address = "2401::1";
  ipconfig.ipv6_prefix_length = 64;
  ipconfig.ipv6_gateway = "fe80::1";
  ipconfig.ipv6_dns_addresses = {"fe80::2"};

  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("fe80::1"), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("fe80::2"), _))
      .WillOnce(Return(true));

  link_monitor_->OnIPConfigChanged(ipconfig);
}

TEST_F(NeighborLinkMonitorTest, SendNeighborProbeMessage) {
  // Only the gateway should be in the wathing list.
  ShillClient::IPConfig ipconfig;
  ipconfig.ipv4_address = "1.2.3.4";
  ipconfig.ipv4_gateway = "1.2.3.5";
  ipconfig.ipv4_prefix_length = 24;
  link_monitor_->OnIPConfigChanged(ipconfig);

  // Creates a RTNL message about the NUD state of the gateway is NUD_REACHABLE
  // now. A probe message should be sent immediately after we know this address.
  std::unique_ptr<shill::RTNLMessage> address_is_reachable(
      CreateNUDStateChangedMessage("1.2.3.5", NUD_REACHABLE));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  link_monitor_->OnNeighborMessage(*address_is_reachable);

  // Another probe message should be sent when the timer is triggered.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  FastForwardOneActiveProbeInterval();

  // If the state changed to NUD_PROBE, we should not probe this address again
  // when the timer is triggered.
  std::unique_ptr<shill::RTNLMessage> address_is_probing(
      CreateNUDStateChangedMessage("1.2.3.5", NUD_PROBE));
  link_monitor_->OnNeighborMessage(*address_is_probing);
  FastForwardOneActiveProbeInterval();

  // The gateway is removed in the kernel. A get request should be sent when the
  // timer is triggered.
  std::unique_ptr<shill::RTNLMessage> address_is_removed(
      CreateNeighborDeletedMessage("1.2.3.5"));
  link_monitor_->OnNeighborMessage(*address_is_removed);
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  FastForwardOneActiveProbeInterval();
}

TEST_F(NeighborLinkMonitorTest, UpdateWatchingEntries) {
  ShillClient::IPConfig ipconfig;
  ipconfig.ipv4_address = "1.2.3.4";
  ipconfig.ipv4_gateway = "1.2.3.5";
  ipconfig.ipv4_dns_addresses = {"1.2.3.6"};
  ipconfig.ipv4_prefix_length = 24;
  link_monitor_->OnIPConfigChanged(ipconfig);

  ipconfig.ipv4_dns_addresses = {"1.2.3.7"};
  // The watching list should be updated to {"1.2.3.5", "1.2.3.7"}, and only
  // "1.2.3.7" is an new entry, so it should be probed immediately.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborGetMessage("1.2.3.7"), _))
      .WillOnce(Return(true));
  link_monitor_->OnIPConfigChanged(ipconfig);

  // Updates both addresses to NUD_PROBE (to avoid the link monitor sending a
  // probe request), and then NUD_REACHABLE state.
  std::unique_ptr<shill::RTNLMessage> addr_5_is_probing(
      CreateNUDStateChangedMessage("1.2.3.5", NUD_PROBE));
  std::unique_ptr<shill::RTNLMessage> addr_5_is_reachable(
      CreateNUDStateChangedMessage("1.2.3.5", NUD_REACHABLE));
  std::unique_ptr<shill::RTNLMessage> addr_7_is_probing(
      CreateNUDStateChangedMessage("1.2.3.7", NUD_PROBE));
  std::unique_ptr<shill::RTNLMessage> addr_7_is_reachable(
      CreateNUDStateChangedMessage("1.2.3.7", NUD_REACHABLE));
  link_monitor_->OnNeighborMessage(*addr_5_is_probing);
  link_monitor_->OnNeighborMessage(*addr_5_is_reachable);
  link_monitor_->OnNeighborMessage(*addr_7_is_probing);
  link_monitor_->OnNeighborMessage(*addr_7_is_reachable);

  // Nothing should happen within one interval.
  task_environment_.FastForwardBy(base::TimeDelta::FromSeconds(30));

  // Checks if probe requests sent for both addresses when the timer is
  // triggered.
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.5"), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*mock_rtnl_handler_,
              DoSendMessage(IsNeighborProbeMessage("1.2.3.7"), _))
      .WillOnce(Return(true));
  FastForwardOneActiveProbeInterval();
}

class NetworkMonitorServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    fake_shill_client_ = shill_helper_.FakeClient();
    monitor_svc_ =
        std::make_unique<NetworkMonitorService>(fake_shill_client_.get());
    mock_rtnl_handler_ = std::make_unique<shill::MockRTNLHandler>();
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<FakeShillClient> fake_shill_client_;
  std::unique_ptr<shill::MockRTNLHandler> mock_rtnl_handler_;
  std::unique_ptr<NetworkMonitorService> monitor_svc_;
};

TEST_F(NetworkMonitorServiceTest, StartRTNLHanlderOnServiceStart) {
  monitor_svc_->rtnl_handler_ = mock_rtnl_handler_.get();
  EXPECT_CALL(*mock_rtnl_handler_, Start(RTMGRP_NEIGH));
  monitor_svc_->Start();
}

TEST_F(NetworkMonitorServiceTest, CallGetDevicePropertiesOnNewDevice) {
  monitor_svc_->rtnl_handler_ = mock_rtnl_handler_.get();
  // Device added before service starts.
  std::vector<dbus::ObjectPath> devices = {dbus::ObjectPath("/device/eth0")};
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
  monitor_svc_->Start();

  // Device added after service starts.
  devices.emplace_back(dbus::ObjectPath("/device/wlan0"));
  fake_shill_client_->NotifyManagerPropertyChange(shill::kDevicesProperty,
                                                  brillo::Any(devices));
  const std::set<std::string>& calls =
      fake_shill_client_->get_device_properties_calls();
  EXPECT_EQ(calls.size(), 2);
  EXPECT_NE(calls.find("eth0"), calls.end());
  EXPECT_NE(calls.find("wlan0"), calls.end());
}

}  // namespace patchpanel
