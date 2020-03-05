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

#include "arc/network/device_manager.h"
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

static AddressManager addr_mgr({
    AddressManager::Guest::ARC,
    AddressManager::Guest::ARC_NET,
});

class MockDeviceManager : public DeviceManagerBase {
 public:
  MockDeviceManager() = default;
  ~MockDeviceManager() = default;

  MOCK_METHOD2(RegisterDeviceAddedHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD2(RegisterDeviceRemovedHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD2(RegisterDeviceIPv6AddressFoundHandler,
               void(GuestMessage::GuestType, const DeviceHandler&));
  MOCK_METHOD1(UnregisterAllGuestHandlers, void(GuestMessage::GuestType));
  MOCK_METHOD1(OnGuestStart, void(GuestMessage::GuestType));
  MOCK_METHOD1(OnGuestStop, void(GuestMessage::GuestType));
  MOCK_METHOD1(ProcessDevices, void(const DeviceHandler&));
  MOCK_CONST_METHOD1(Exists, bool(const std::string& name));
  MOCK_CONST_METHOD1(FindByHostInterface, Device*(const std::string& ifname));
  MOCK_CONST_METHOD1(FindByGuestInterface, Device*(const std::string& ifname));
  MOCK_METHOD1(Add, bool(const std::string&));
  MOCK_METHOD2(AddWithContext,
               bool(const std::string&, std::unique_ptr<Device::Context>));
  MOCK_METHOD1(Remove, bool(const std::string&));
  MOCK_METHOD2(StartForwarding, void(const Device&, const std::string&));
  MOCK_METHOD2(StopForwarding, void(const Device&, const std::string&));
  MOCK_CONST_METHOD0(addr_mgr, AddressManager*());
};

class MockTrafficForwarder : public TrafficForwarder {
 public:
  MockTrafficForwarder() = default;
  ~MockTrafficForwarder() = default;

  MOCK_METHOD5(StartForwarding,
               void(const std::string& ifname_physical,
                    const std::string& ifname_virtual,
                    uint32_t ipv4_addr_virtual,
                    bool ipv6,
                    bool multicast));

  MOCK_METHOD4(StopForwarding,
               void(const std::string& ifname_physical,
                    const std::string& ifname_virtual,
                    bool ipv6,
                    bool multicast));
};

std::unique_ptr<Device> MakeDevice(const std::string& name,
                                   const std::string& host,
                                   const std::string& guest,
                                   bool is_arcvm = false) {
  Device::Options opt{
      .use_default_interface = is_arcvm,
  };
  auto subnet = addr_mgr.AllocateIPv4Subnet(
      opt.is_android ? AddressManager::Guest::ARC
                     : AddressManager::Guest::ARC_NET);
  auto addr0 = subnet->AllocateAtOffset(0);
  auto addr1 = subnet->AllocateAtOffset(1);
  auto cfg = std::make_unique<Device::Config>(
      host, guest, addr_mgr.GenerateMacAddress(), std::move(subnet),
      std::move(addr0), std::move(addr1));
  return std::make_unique<Device>(name, std::move(cfg), opt, GuestMessage::ARC);
}

}  // namespace

class ArcServiceTest : public testing::Test {
 public:
  ArcServiceTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    shill_client_ = shill_helper_.Client();
  }

  std::unique_ptr<ArcService> NewService() {
    EXPECT_CALL(dev_mgr_, RegisterDeviceAddedHandler(_, _));
    EXPECT_CALL(dev_mgr_, RegisterDeviceRemovedHandler(_, _));
    EXPECT_CALL(dev_mgr_, UnregisterAllGuestHandlers(_));
    EXPECT_CALL(dev_mgr_, addr_mgr()).WillRepeatedly(Return(&addr_mgr_));

    arc_networkd::test::guest = GuestMessage::ARC;
    return std::make_unique<ArcService>(shill_client_.get(), &dev_mgr_,
                                        datapath_.get(), &addr_mgr_,
                                        &forwarder_);
  }

  FakeShillClientHelper shill_helper_;
  std::unique_ptr<ShillClient> shill_client_;
  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
  MockTrafficForwarder forwarder_;
  std::unique_ptr<MockDatapath> datapath_;
  std::unique_ptr<FakeProcessRunner> runner_;
};

TEST_F(ArcServiceTest, VerifyOnDeviceAddedDatapath) {
  EXPECT_CALL(*datapath_,
              AddBridge(StrEq("arc_eth0"), Ipv4Addr(100, 115, 92, 9), 30))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_,
              AddInboundIPv4DNAT(StrEq("eth0"), StrEq("100.115.92.10")))
      .WillOnce(Return(true));
  EXPECT_CALL(*datapath_, AddOutboundIPv4(StrEq("arc_eth0")))
      .WillOnce(Return(true));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceAdded(dev.get());
}

TEST_F(ArcServiceTest, VerifyOnDeviceRemovedDatapath) {
  EXPECT_CALL(*datapath_, RemoveBridge(StrEq("arc_eth0")));

  auto dev = MakeDevice("eth0", "arc_eth0", "eth0");
  ASSERT_TRUE(dev);
  NewService()->OnDeviceRemoved(dev.get());
}

// ContainerImpl

class ContainerImplTest : public testing::Test {
 public:
  ContainerImplTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    EXPECT_CALL(dev_mgr_, addr_mgr()).WillRepeatedly(Return(&addr_mgr_));
  }

  std::unique_ptr<ArcService::ContainerImpl> Impl(bool start = true) {
    auto impl = std::make_unique<ArcService::ContainerImpl>(
        datapath_.get(), &addr_mgr_, &forwarder_, GuestMessage::ARC);
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
      EXPECT_CALL(dev_mgr_, StartForwarding(_, _)).Times(AnyNumber());

      impl->Start(kTestPID);
    }
    return impl;
  }

  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
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
  EXPECT_CALL(forwarder_, StartForwarding(_, _, _, _, _)).Times(0);
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
  EXPECT_CALL(forwarder_, StartForwarding(StrEq("eth0"), StrEq("arc_eth0"),
                                          Ipv4Addr(100, 115, 92, 10), _, _));
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
  VmImplTest()
      : testing::Test(),
        addr_mgr_({
            AddressManager::Guest::ARC,
            AddressManager::Guest::ARC_NET,
            AddressManager::Guest::VM_ARC,
        }) {}

 protected:
  void SetUp() override {
    runner_ = std::make_unique<FakeProcessRunner>();
    runner_->Capture(false);
    datapath_ = std::make_unique<MockDatapath>(runner_.get());
    shill_client_ = helper_.FakeClient();
    shill_client_->SetFakeDefaultInterface("eth0");
    EXPECT_CALL(dev_mgr_, addr_mgr()).WillRepeatedly(Return(&addr_mgr_));
  }

  std::unique_ptr<ArcService::VmImpl> Impl(bool start = true) {
    auto impl = std::make_unique<ArcService::VmImpl>(
        shill_client_.get(), datapath_.get(), &addr_mgr_, &forwarder_);
    if (start) {
      impl->Start(kTestCID);
    }

    return impl;
  }

  AddressManager addr_mgr_;
  MockDeviceManager dev_mgr_;
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
  EXPECT_CALL(forwarder_, StartForwarding(StrEq("eth0"), StrEq("arc_br1"),
                                          kArcVmGuestIP, true, true));
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
  EXPECT_CALL(forwarder_, StartForwarding(StrEq("eth0"), StrEq("arc_br1"),
                                          kArcVmGuestIP, true, true));
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
