// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/traffic_monitor.h"

#include <string>
#include <vector>

#include <base/bind.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>
#include <netinet/in.h>

#include "shill/ipconfig.h"
#include "shill/mock_connection_info_reader.h"
#include "shill/mock_control.h"
#include "shill/mock_device.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_socket_info_reader.h"

using base::Bind;
using base::StringPrintf;
using base::Unretained;
using std::string;
using std::vector;
using testing::_;
using testing::Mock;
using testing::NiceMock;
using testing::ReturnRef;
using testing::Test;

namespace shill {

class TrafficMonitorTest : public Test {
 public:
  static const char kLocalIpAddr[];
  static const char kLocalIp6Addr[];
  static const uint16_t kLocalPort1;
  static const uint16_t kLocalPort2;
  static const uint16_t kLocalPort3;
  static const uint16_t kLocalPort4;
  static const uint16_t kLocalPort5;
  static const char kRemoteIpAddr[];
  static const char kRemoteIp6Addr[];
  static const uint16_t kRemotePort;
  static const uint64_t kTxQueueLength1;
  static const uint64_t kTxQueueLength2;
  static const uint64_t kTxQueueLength3;
  static const uint64_t kTxQueueLength4;

  TrafficMonitorTest()
      : manager_(&control_, &dispatcher_, nullptr),
        device_(new MockDevice(&manager_, "netdev0", "00:11:22:33:44:55", 1)),
        ipconfig_(new IPConfig(&control_, "netdev0")),
        ip6config_(new IPConfig(&control_, "netdev0")),
        mock_socket_info_reader_(new MockSocketInfoReader),
        mock_connection_info_reader_(new MockConnectionInfoReader),
        monitor_(
            device_,
            &dispatcher_,
            Bind(&TrafficMonitorTest::OnNoOutgoingPackets, Unretained(this))),
        local_addr_(IPAddress::kFamilyIPv4),
        local_addr6_(IPAddress::kFamilyIPv6),
        remote_addr_(IPAddress::kFamilyIPv4),
        remote_addr6_(IPAddress::kFamilyIPv6) {}

  MOCK_METHOD(void, OnNoOutgoingPackets, (int));

 protected:
  void SetUp() override {
    EXPECT_TRUE(local_addr_.SetAddressFromString(kLocalIpAddr));
    EXPECT_TRUE(remote_addr_.SetAddressFromString(kRemoteIpAddr));
    EXPECT_TRUE(local_addr6_.SetAddressFromString(kLocalIp6Addr));
    EXPECT_TRUE(remote_addr6_.SetAddressFromString(kRemoteIp6Addr));

    monitor_.socket_info_reader_.reset(
        mock_socket_info_reader_);  // Passes ownership
    monitor_.connection_info_reader_.reset(
        mock_connection_info_reader_);  // Passes ownership

    IPConfig::Properties ipconfig_properties;
    ipconfig_properties.address_family = IPAddress::kFamilyIPv4;
    ipconfig_properties.address = kLocalIpAddr;
    ipconfig_->set_properties(ipconfig_properties);

    device_->set_ipconfig(ipconfig_);

    // Initialize ip6config_ too, but default to IPv4-only.
    IPConfig::Properties ip6config_properties;
    ip6config_properties.address_family = IPAddress::kFamilyIPv6;
    ip6config_properties.address = kLocalIp6Addr;
    ip6config_->set_properties(ip6config_properties);
  }

  void VerifyStopped() {
    EXPECT_TRUE(monitor_.sample_traffic_callback_.IsCancelled());
    EXPECT_EQ(0, monitor_.accummulated_congested_tx_queues_samples_);
  }

  void VerifyStarted() {
    EXPECT_FALSE(monitor_.sample_traffic_callback_.IsCancelled());
  }

  void SetupMockSocketInfos(const vector<SocketInfo>& socket_infos) {
    mock_socket_infos_ = socket_infos;
    EXPECT_CALL(*mock_socket_info_reader_, LoadTcpSocketInfo(_))
        .WillRepeatedly(
            Invoke(this, &TrafficMonitorTest::MockLoadTcpSocketInfo));
  }

  void SetupMockConnectionInfos(
      const vector<ConnectionInfo>& connection_infos) {
    mock_connection_infos_ = connection_infos;
    EXPECT_CALL(*mock_connection_info_reader_, LoadConnectionInfo(_))
        .WillRepeatedly(
            Invoke(this, &TrafficMonitorTest::MockLoadConnectionInfo));
  }

  bool MockLoadTcpSocketInfo(vector<SocketInfo>* info_list) {
    *info_list = mock_socket_infos_;
    return true;
  }

  bool MockLoadConnectionInfo(vector<ConnectionInfo>* info_list) {
    *info_list = mock_connection_infos_;
    return true;
  }

  string FormatIPPort(const IPAddress& ip, const uint16_t port) {
    return StringPrintf("%s:%d", ip.ToString().c_str(), port);
  }

  using IPPortToTxQueueLengthMap = TrafficMonitor::IPPortToTxQueueLengthMap;

  IPPortToTxQueueLengthMap BuildIPPortToTxQueueLength(
      const std::vector<SocketInfo>& socket_infos) {
    return monitor_.BuildIPPortToTxQueueLength(socket_infos);
  }

  MockControl control_;
  NiceMock<MockEventDispatcher> dispatcher_;
  MockManager manager_;
  scoped_refptr<MockDevice> device_;
  scoped_refptr<IPConfig> ipconfig_;
  scoped_refptr<IPConfig> ip6config_;
  MockSocketInfoReader* mock_socket_info_reader_;
  MockConnectionInfoReader* mock_connection_info_reader_;
  TrafficMonitor monitor_;
  vector<SocketInfo> mock_socket_infos_;
  vector<ConnectionInfo> mock_connection_infos_;
  IPAddress local_addr_;
  IPAddress local_addr6_;
  IPAddress remote_addr_;
  IPAddress remote_addr6_;
};

// static
const char TrafficMonitorTest::kLocalIpAddr[] = "127.0.0.1";
const char TrafficMonitorTest::kLocalIp6Addr[] = "::1";
const uint16_t TrafficMonitorTest::kLocalPort1 = 1234;
const uint16_t TrafficMonitorTest::kLocalPort2 = 2345;
const uint16_t TrafficMonitorTest::kLocalPort3 = 3456;
const uint16_t TrafficMonitorTest::kLocalPort4 = 4567;
const uint16_t TrafficMonitorTest::kLocalPort5 = 4567;
const char TrafficMonitorTest::kRemoteIpAddr[] = "192.168.1.1";
const char TrafficMonitorTest::kRemoteIp6Addr[] = "fd00::1";
const uint16_t TrafficMonitorTest::kRemotePort = 5678;
const uint64_t TrafficMonitorTest::kTxQueueLength1 = 111;
const uint64_t TrafficMonitorTest::kTxQueueLength2 = 222;
const uint64_t TrafficMonitorTest::kTxQueueLength3 = 333;
const uint64_t TrafficMonitorTest::kTxQueueLength4 = 444;

TEST_F(TrafficMonitorTest, StartAndStop) {
  // Stop without start
  monitor_.Stop();
  VerifyStopped();

  // Normal start
  monitor_.Start();
  VerifyStarted();

  // Stop after start
  monitor_.Stop();
  VerifyStopped();

  // Stop again without start
  monitor_.Stop();
  VerifyStopped();
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthValid) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(1, tx_queue_lengths.size());
  string ip_port = FormatIPPort(local_addr_, TrafficMonitorTest::kLocalPort1);
  EXPECT_EQ(TrafficMonitorTest::kTxQueueLength1, tx_queue_lengths[ip_port]);
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthInvalidDevice) {
  IPAddress foreign_ip_addr(IPAddress::kFamilyIPv4);
  foreign_ip_addr.SetAddressFromString("192.167.1.1");
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, foreign_ip_addr,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(0, tx_queue_lengths.size());
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthZero) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort, 0, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(0, tx_queue_lengths.size());
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthInvalidConnectionState) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateSynSent, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(0, tx_queue_lengths.size());
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthInvalidTimerState) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateNoTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(0, tx_queue_lengths.size());
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthMultipleEntries) {
  device_->set_ip6config(ip6config_);

  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateSynSent, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateNoTimerPending),
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort2, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength2, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort3, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength3, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort4, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength4, 0,
                 SocketInfo::kTimerStateNoTimerPending),
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort5, remote_addr_,
                 TrafficMonitorTest::kRemotePort, 0, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr6_,
                 TrafficMonitorTest::kLocalPort1, remote_addr6_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(3, tx_queue_lengths.size());
  string ip_port = FormatIPPort(local_addr_, TrafficMonitorTest::kLocalPort2);
  EXPECT_EQ(kTxQueueLength2, tx_queue_lengths[ip_port]);
  ip_port = FormatIPPort(local_addr_, TrafficMonitorTest::kLocalPort3);
  EXPECT_EQ(kTxQueueLength3, tx_queue_lengths[ip_port]);
  ip_port = FormatIPPort(local_addr6_, TrafficMonitorTest::kLocalPort1);
  EXPECT_EQ(kTxQueueLength1, tx_queue_lengths[ip_port]);
}

TEST_F(TrafficMonitorTest, BuildIPPortToTxQueueLengthIPv6) {
  device_->set_ip6config(ip6config_);
  device_->set_ipconfig(nullptr);

  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr6_,
                 TrafficMonitorTest::kLocalPort1, remote_addr6_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  IPPortToTxQueueLengthMap tx_queue_lengths =
      BuildIPPortToTxQueueLength(socket_infos);
  EXPECT_EQ(1, tx_queue_lengths.size());
  string ip_port = FormatIPPort(local_addr6_, TrafficMonitorTest::kLocalPort1);
  EXPECT_EQ(kTxQueueLength1, tx_queue_lengths[ip_port]);
}

TEST_F(TrafficMonitorTest, SampleTrafficStuckTxQueueSameQueueLength) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  // Mimic same queue length by using same mock socket info.
  EXPECT_CALL(*this, OnNoOutgoingPackets(
                         TrafficMonitor::kNetworkProblemCongestedTxQueue));
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  // Perform another sampling pass and make sure the callback is only
  // triggered once.
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
}

TEST_F(TrafficMonitorTest, SampleTrafficStuckTxQueueIncreasingQueueLength) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1 + 1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(
                         TrafficMonitor::kNetworkProblemCongestedTxQueue));
  monitor_.SampleTraffic();
}

TEST_F(TrafficMonitorTest, SampleTrafficStuckTxQueueVariousQueueLengths) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength2, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength2, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(
                         TrafficMonitor::kNetworkProblemCongestedTxQueue));
  monitor_.SampleTraffic();
}

TEST_F(TrafficMonitorTest, SampleTrafficUnstuckTxQueueZeroQueueLength) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();

  socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort, 0, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  monitor_.SampleTraffic();
  EXPECT_EQ(0, monitor_.accummulated_congested_tx_queues_samples_);
}

TEST_F(TrafficMonitorTest, SampleTrafficUnstuckTxQueueNoConnection) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();

  socket_infos.clear();
  SetupMockSocketInfos(socket_infos);
  monitor_.SampleTraffic();
  EXPECT_EQ(0, monitor_.accummulated_congested_tx_queues_samples_);
}

TEST_F(TrafficMonitorTest, SampleTrafficUnstuckTxQueueStateChanged) {
  vector<SocketInfo> socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateEstablished, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort,
                 TrafficMonitorTest::kTxQueueLength1, 0,
                 SocketInfo::kTimerStateRetransmitTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();

  socket_infos = {
      SocketInfo(SocketInfo::kConnectionStateClose, local_addr_,
                 TrafficMonitorTest::kLocalPort1, remote_addr_,
                 TrafficMonitorTest::kRemotePort, 0, 0,
                 SocketInfo::kTimerStateNoTimerPending),
  };
  SetupMockSocketInfos(socket_infos);
  monitor_.SampleTraffic();
  EXPECT_EQ(0, monitor_.accummulated_congested_tx_queues_samples_);
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsTimedOut) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, true,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  // Make sure the no routing event is not fired before the threshold is
  // exceeded.
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 1; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
  Mock::VerifyAndClearExpectations(this);

  // This call should cause the threshold to exceed.
  EXPECT_CALL(*this,
              OnNoOutgoingPackets(TrafficMonitor::kNetworkProblemDNSFailure))
      .Times(1);
  monitor_.SampleTraffic();
  Mock::VerifyAndClearExpectations(this);

  // Make sure the event is only fired once.
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsOutstanding) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds + 1, true,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 0; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsSuccessful) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, false,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 1; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsFailureThenSuccess) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, true,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 1; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
  Mock::VerifyAndClearExpectations(this);

  connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, false,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  monitor_.SampleTraffic();
  EXPECT_EQ(0, monitor_.accummulated_dns_failures_samples_);
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsTimedOutInvalidProtocol) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_TCP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, true,
          local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          local_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 0; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsTimedOutInvalidSourceIp) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(
          IPPROTO_UDP, TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, true,
          remote_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
          TrafficMonitor::kDnsPort, remote_addr_, TrafficMonitor::kDnsPort,
          remote_addr_, TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 0; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsTimedOutOutsideTimeWindow) {
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(IPPROTO_UDP,
                     TrafficMonitor::kDnsTimedOutThresholdSeconds -
                         TrafficMonitor::kSamplingIntervalMilliseconds / 1000,
                     true, remote_addr_, TrafficMonitorTest::kLocalPort1,
                     remote_addr_, TrafficMonitor::kDnsPort, remote_addr_,
                     TrafficMonitor::kDnsPort, remote_addr_,
                     TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 0; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficNonDnsTimedOut) {
  const uint16_t kNonDnsPort = 54;
  vector<ConnectionInfo> connection_infos = {
      ConnectionInfo(IPPROTO_UDP,
                     TrafficMonitor::kDnsTimedOutThresholdSeconds - 1, true,
                     local_addr_, TrafficMonitorTest::kLocalPort1, remote_addr_,
                     kNonDnsPort, remote_addr_, kNonDnsPort, local_addr_,
                     TrafficMonitorTest::kLocalPort1),
  };
  SetupMockConnectionInfos(connection_infos);
  EXPECT_CALL(*this, OnNoOutgoingPackets(_)).Times(0);
  for (int count = 0; count < TrafficMonitor::kMinimumFailedSamplesToTrigger;
       ++count) {
    monitor_.SampleTraffic();
  }
}

TEST_F(TrafficMonitorTest, SampleTrafficDnsStatsReset) {
  vector<ConnectionInfo> connection_infos;
  SetupMockConnectionInfos(connection_infos);
  monitor_.accummulated_dns_failures_samples_ = 1;
  monitor_.SampleTraffic();
  EXPECT_EQ(0, monitor_.accummulated_dns_failures_samples_);
}

}  // namespace shill
