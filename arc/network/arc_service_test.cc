// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/network/arc_service.h"

#include <net/if.h>

#include <utility>
#include <vector>

#include <base/bind.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/network/address_manager.h"
#include "arc/network/fake_process_runner.h"
#include "arc/network/fake_shill_client.h"
#include "arc/network/mock_datapath.h"
#include "arc/network/net_util.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;
using testing::ReturnRef;
using testing::StrEq;

namespace arc_networkd {
namespace {
constexpr pid_t kTestPID = -2;
constexpr uint32_t kTestCID = 2;
constexpr uint32_t kArcHostIP = Ipv4Addr(100, 115, 92, 1);
constexpr uint32_t kArcGuestIP = Ipv4Addr(100, 115, 92, 2);
constexpr uint32_t kArcVmHostIP = Ipv4Addr(100, 115, 92, 5);
constexpr uint32_t kArcVmGuestIP = Ipv4Addr(100, 115, 92, 6);

class MockTrafficForwarder : public TrafficForwarder {
 public:
  MockTrafficForwarder() = default;
  ~MockTrafficForwarder() = default;

  MOCK_METHOD4(StartForwarding,
               void(const std::string& ifname_physical,
                    const std::string& ifname_virtual,
                    bool ipv6,
                    bool multicast));

  MOCK_METHOD4(StopForwarding,
               void(const std::string& ifname_physical,
                    const std::string& ifname_virtual,
                    bool ipv6,
                    bool multicast));
};

class MockImpl : public ArcService::Impl {
 public:
  MockImpl() = default;
  ~MockImpl() = default;

  MOCK_CONST_METHOD0(guest, GuestMessage::GuestType());
  MOCK_CONST_METHOD0(id, uint32_t());
  MOCK_METHOD1(Start, bool(uint32_t));
  MOCK_METHOD1(Stop, void(uint32_t));
  MOCK_CONST_METHOD1(IsStarted, bool(uint32_t*));
  MOCK_METHOD1(OnStartDevice, bool(Device*));
  MOCK_METHOD1(OnStopDevice, void(Device*));
  MOCK_METHOD2(OnDefaultInterfaceChanged,
               void(const std::string&, const std::string&));
};

}  // namespace

class ArcServiceTest : public testing::Test {
 public:
  ArcServiceTest() : testing::Test() {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    shill_client_ = shill_helper_.Client();
    addr_mgr_ = std::make_unique<AddressManager>();
  }

  std::unique_ptr<ArcService> NewService() {
    arc_networkd::test::guest = GuestMessage::ARC;
    return std::make_unique<ArcService>(shill_client_.get(), datapath_.get(),
                                        addr_mgr_.get(), &forwarder_, false);
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<ShillClient> shill_client_;
  std::unique_ptr<AddressManager> addr_mgr_;
  MockTrafficForwarder forwarder_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
};

TEST_F(ArcServiceTest, StartDevice) {
  EXPECT_CALL(*datapath_,
              AddBridge(StrEq("arc_eth0"), Ipv4Addr(100, 115, 92, 9), 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(StrEq("eth0"), StrEq("100.115.92.10")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arc_eth0")))
      .WillOnce(Return(true));

  auto svc = NewService();
  auto impl = std::make_unique<MockImpl>();
  auto* mock_impl = impl.get();
  svc->impl_ = std::move(impl);

  EXPECT_CALL(*mock_impl, guest()).WillRepeatedly(Return(GuestMessage::ARC));
  EXPECT_CALL(*mock_impl, IsStarted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_impl, OnStartDevice(_)).WillOnce(Return(true));
  svc->AddDevice("eth0");
  EXPECT_TRUE(svc->devices_.find("eth0") != svc->devices_.end());
}

TEST_F(ArcServiceTest, StopDevice) {
  EXPECT_CALL(*datapath_, RemoveOutboundIPv4(StrEq("arc_eth0")));
  EXPECT_CALL(*datapath_,
              RemoveInboundIPv4DNAT(StrEq("eth0"), StrEq("100.115.92.10")));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));

  auto svc = NewService();
  auto impl = std::make_unique<MockImpl>();
  auto* mock_impl = impl.get();
  svc->impl_ = std::move(impl);

  EXPECT_CALL(*mock_impl, guest()).WillRepeatedly(Return(GuestMessage::ARC));
  EXPECT_CALL(*mock_impl, IsStarted(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_impl, OnStopDevice(_));
  svc->AddDevice("eth0");
  svc->RemoveDevice("eth0");
  EXPECT_TRUE(svc->devices_.find("eth0") == svc->devices_.end());
}

// ContainerImpl

class ContainerImplTest : public testing::Test {
 public:
  ContainerImplTest() : testing::Test() {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    addr_mgr_ = std::make_unique<AddressManager>();
  }

  std::unique_ptr<ArcService::ContainerImpl> Impl(bool start = true) {
    auto impl = std::make_unique<ArcService::ContainerImpl>(
        datapath_.get(), addr_mgr_.get(), &forwarder_, GuestMessage::ARC);
    if (start) {
      // Set expectations for tests that want it started already.
      EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
          .WillOnce(Return(true));
      EXPECT_CALL(*datapath_,
                  AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
          .WillOnce(Return(true));
      EXPECT_CALL(*datapath_, ConfigureInterface(StrEq("arc0"), _, kArcGuestIP,
                                                 30, true, _))
          .WillOnce(Return(true));
      EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_arc0"), true))
          .WillOnce(Return(true));
      EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("veth_arc0")))
          .WillOnce(Return(true));

      impl->Start(kTestPID);
    }
    return impl;
  }

  std::unique_ptr<Device> MakeDevice(const std::string& name,
                                     const std::string& host,
                                     const std::string& guest,
                                     bool is_arcvm = false) {
    Device::Options opt{
        .use_default_interface = is_arcvm,
    };
    auto subnet = addr_mgr_->AllocateIPv4Subnet(AddressManager::Guest::ARC_NET);
    auto addr0 = subnet->AllocateAtOffset(0);
    auto addr1 = subnet->AllocateAtOffset(1);
    auto cfg = std::make_unique<Device::Config>(
        host, guest, addr_mgr_->GenerateMacAddress(), std::move(subnet),
        std::move(addr0), std::move(addr1));
    return std::make_unique<Device>(name, std::move(cfg), opt);
  }

  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
  MockTrafficForwarder forwarder_;
};

TEST_F(ContainerImplTest, Start) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("arc0"), _, kArcGuestIP, 30, true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_arc0"), true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arcbr0"), StrEq("veth_arc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(forwarder_, StartForwarding(_, _, _, _)).Times(0);
  Impl(false)->Start(kTestPID);
}

TEST_F(ContainerImplTest, Start_FailsToCreateInterface_Android) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, ConfigureInterface(_, _, _, _, _, _)).Times(0);
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  Impl(false)->Start(kTestPID);
}

TEST_F(ContainerImplTest, OnStartDevice_FailsToConfigureInterface_Android) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arcbr0"), kArcHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_arc0"), StrEq("arc0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("arc0"), _, kArcGuestIP, 30, true, _))
      .WillOnce(Return(false));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_arc0"), true)).Times(0);
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("arc0")));
  EXPECT_CALL(*datapath_, RemoveBridge(_)).Times(0);
  Impl(false)->Start(kTestPID);
}

TEST_F(ContainerImplTest, OnStartDevice) {
  EXPECT_CALL(*datapath_,
              AddVirtualInterfacePair(StrEq("veth_eth0"), StrEq("eth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              ConfigureInterface(StrEq("eth0"), _, Ipv4Addr(100, 115, 92, 10),
                                 30, true, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, ToggleInterface(StrEq("veth_eth0"), true))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_eth0"), StrEq("veth_eth0")))
      .WillOnce(Return(true));
  EXPECT_CALL(forwarder_,
              StartForwarding(StrEq("eth0"), StrEq("arc_eth0"), _, _));
  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnStartDevice(dev.get());
}

TEST_F(ContainerImplTest, Stop) {
  EXPECT_CALL(*datapath_,
              MaskInterfaceFlags(StrEq("arcbr0"), IFF_DEBUG, IFF_UP));
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("veth_arc0")));
  EXPECT_CALL(forwarder_, StopForwarding(_, _, _, _)).Times(0);

  Impl()->Stop(kTestPID);
}

TEST_F(ContainerImplTest, OnStopDevice) {
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("veth_eth0")));
  EXPECT_CALL(forwarder_,
              StopForwarding(StrEq("eth0"), StrEq("arc_eth0"), _, _));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  Impl()->OnStopDevice(dev.get());
}

// VM Impl

class VmImplTest : public testing::Test {
 public:
  VmImplTest() : testing::Test() {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    shill_client_ = helper_.FakeClient();
    shill_client_->SetFakeDefaultInterface("eth0");
    addr_mgr_ = std::make_unique<AddressManager>();
  }

  std::unique_ptr<ArcService::VmImpl> Impl(bool start = true,
                                           bool multinet = false) {
    auto impl = std::make_unique<ArcService::VmImpl>(
        shill_client_.get(), datapath_.get(), addr_mgr_.get(), &forwarder_,
        multinet);
    if (start) {
      impl->Start(kTestCID);
    }

    return impl;
  }

  std::unique_ptr<AddressManager> addr_mgr_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
  std::unique_ptr<FakeShillClient> shill_client_;
  FakeShillClientHelper helper_;
  MockTrafficForwarder forwarder_;
};

TEST_F(VmImplTest, Start) {
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_br1"), kArcVmHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddLegacyIPv4DNAT(StrEq(IPv4AddressToString(kArcVmGuestIP))))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arc_br1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), nullptr, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_br1"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  // OnDefaultInterfaceChanged
  EXPECT_CALL(forwarder_,
              StopForwarding(StrEq(""), StrEq("arc_br1"), true, true));
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4InboundDNAT());
  EXPECT_CALL(forwarder_,
              StartForwarding(StrEq("eth0"), StrEq("arc_br1"), true, true));
  EXPECT_CALL(*datapath_, AddLegacyIPv4InboundDNAT(StrEq("eth0")));

  Impl(false)->Start(kTestCID);
}

TEST_F(VmImplTest, Stop) {
  // Start
  EXPECT_CALL(*datapath_, AddBridge(StrEq("arc_br1"), kArcVmHostIP, 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddLegacyIPv4DNAT(StrEq(IPv4AddressToString(kArcVmGuestIP))))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arc_br1")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddTAP(StrEq(""), nullptr, nullptr, StrEq("crosvm")))
      .WillOnce(Return("vmtap0"));
  EXPECT_CALL(*datapath_, AddToBridge(StrEq("arc_br1"), StrEq("vmtap0")))
      .WillOnce(Return(true));
  // OnDefaultInterfaceChanged
  EXPECT_CALL(forwarder_,
              StopForwarding(StrEq(""), StrEq("arc_br1"), true, true));
  EXPECT_CALL(forwarder_,
              StartForwarding(StrEq("eth0"), StrEq("arc_br1"), true, true));
  EXPECT_CALL(*datapath_, AddLegacyIPv4InboundDNAT(StrEq("eth0")));

  // Stop
  EXPECT_CALL(*datapath_, RemoveOutboundIPv4(StrEq("arc_br1")));
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4DNAT());
  EXPECT_CALL(*datapath_, RemoveInterface(StrEq("vmtap0")));
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_br1")));
  // OnDefaultInterfaceChanged
  EXPECT_CALL(*datapath_, RemoveLegacyIPv4InboundDNAT())
      .Times(2);  // +1 for Start
  EXPECT_CALL(forwarder_,
              StopForwarding(StrEq("eth0"), StrEq("arc_br1"), true, true));
  Impl()->Stop(kTestCID);
}

}  // namespace arc_networkd
